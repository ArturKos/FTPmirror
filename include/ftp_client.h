/**
 * @file ftp_client.h
 * @brief Minimal FTP client library: connect, authenticate, list, retrieve, mirror.
 *
 * The library implements a small subset of RFC 959 sufficient to mirror a
 * remote directory tree from a public FTP server over anonymous or
 * user/password authentication, using passive-mode data connections.
 *
 * Pure parsing helpers are exposed separately from the networking layer so
 * that they can be unit-tested without a live server.
 */
#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/** Default control-connection port for FTP. */
#define FTP_DEFAULT_PORT 21

/** Size of the internal response/read buffer. */
#define FTP_BUFFER_SIZE 4096

/** Maximum length of a remote path stored in an ::ftp_url. */
#define FTP_MAX_PATH 1024

/** Maximum length of a hostname stored in an ::ftp_url. */
#define FTP_MAX_HOST 256

/** Maximum length of a username stored in an ::ftp_url. */
#define FTP_MAX_USER 128

/** Maximum length of a single directory listing entry name. */
#define FTP_MAX_NAME 256

/**
 * @brief Parsed representation of an FTP URL.
 *
 * The URL syntax accepted by ::ftp_parse_url is
 * `[user@]host[/path]`. When @c user is absent the structure's
 * @c user field is set to the empty string and callers should fall back
 * to anonymous authentication. When @c path is absent it defaults to "/".
 */
typedef struct {
    char host[FTP_MAX_HOST]; /**< FTP server hostname, without user or path. */
    char user[FTP_MAX_USER]; /**< Username, or empty string for anonymous. */
    char path[FTP_MAX_PATH]; /**< Remote path starting with '/'. */
} ftp_url;

/**
 * @brief Kind of a directory entry returned by ::ftp_parse_list_line.
 */
typedef enum {
    FTP_ENTRY_FILE,      /**< Regular file. */
    FTP_ENTRY_DIRECTORY, /**< Subdirectory. */
    FTP_ENTRY_SYMLINK,   /**< Symbolic link (treated as file when mirroring). */
    FTP_ENTRY_OTHER      /**< Anything else (device node, socket, ...). */
} ftp_entry_kind;

/**
 * @brief Single parsed UNIX-style LIST entry.
 */
typedef struct {
    ftp_entry_kind kind;        /**< File type. */
    long long size;             /**< Size in bytes (0 for directories). */
    char name[FTP_MAX_NAME];    /**< Base name, without path. */
} ftp_list_entry;

/**
 * @brief Session state used by the FTP networking API.
 *
 * The control socket stays open for the lifetime of the session. Data
 * connections are opened on demand for each LIST/RETR command and closed
 * as soon as the transfer finishes.
 */
typedef struct {
    int control_socket;                 /**< Control-connection file descriptor, or -1 when closed. */
    char last_response[FTP_BUFFER_SIZE];/**< Most recent raw server response. */
} ftp_session;

/* ------------------------------------------------------------------ */
/* Pure parsing helpers (testable without a live server).              */
/* ------------------------------------------------------------------ */

/**
 * @brief Parse an FTP URL of the form `[user@]host[/path]`.
 *
 * @param url Null-terminated input URL.
 * @param out Output structure filled on success; untouched on failure.
 * @return @c true if @p url is non-empty and a hostname could be extracted,
 *         @c false otherwise.
 *
 * When the user part is missing, @c out->user is set to the empty string.
 * When the path part is missing, @c out->path defaults to "/".
 */
bool ftp_parse_url(const char *url, ftp_url *out);

/**
 * @brief Extract the numeric reply code from a raw FTP response line.
 *
 * Returns the leading three-digit status code or -1 if the response does
 * not begin with three digits.
 *
 * @param response Raw response text as received from the server.
 * @return The 3-digit reply code, or -1 on malformed input.
 */
int ftp_parse_response_code(const char *response);

/**
 * @brief Parse a @c 227 PASV response into an IPv4 address and port.
 *
 * The expected payload is of the form `227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)`.
 *
 * @param response  Raw response string.
 * @param ip_out    Buffer receiving the dotted-quad IPv4 address.
 * @param ip_size   Size of @p ip_out; must be at least 16 bytes.
 * @param port_out  Output port number (p1*256 + p2).
 * @return @c true on success, @c false on malformed input.
 */
bool ftp_parse_pasv_response(const char *response,
                             char *ip_out,
                             size_t ip_size,
                             int *port_out);

/**
 * @brief Parse a single UNIX-style LIST line into an ::ftp_list_entry.
 *
 * Accepts the usual nine-field layout produced by most BSD/Linux FTP
 * daemons, e.g. `-rw-r--r-- 1 user group 1234 Jan  5 12:34 file.txt`.
 * Current-directory (".") and parent-directory ("..") names are rejected
 * so callers do not recurse back into them.
 *
 * @param line Raw listing line (without trailing CR/LF).
 * @param out  Output structure filled on success.
 * @return @c true when a directory entry was recognised, @c false otherwise.
 */
bool ftp_parse_list_line(const char *line, ftp_list_entry *out);

/**
 * @brief Concatenate @p base and @p name with exactly one separator between them.
 *
 * The function normalises trailing and leading slashes so that repeated
 * calls do not produce `//` in the result.
 *
 * @param base      Base directory path.
 * @param name      Name or sub-path to append.
 * @param out       Output buffer.
 * @param out_size  Size of @p out in bytes.
 * @return @c true if the joined path fit, @c false if it was truncated.
 */
bool ftp_join_path(const char *base, const char *name, char *out, size_t out_size);

/* ------------------------------------------------------------------ */
/* Networking API.                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise a session struct to a well-defined empty state.
 *
 * @param session Session to reset.
 */
void ftp_session_init(ftp_session *session);

/**
 * @brief Open a control connection to @p host on @p port and consume the welcome banner.
 *
 * @param session Pre-initialised session.
 * @param host    Hostname or dotted-quad address.
 * @param port    TCP port (usually ::FTP_DEFAULT_PORT).
 * @return @c true on success, @c false on DNS, @c socket(2), @c connect(2) or banner failure.
 */
bool ftp_session_connect(ftp_session *session, const char *host, int port);

/**
 * @brief Authenticate using USER / PASS.
 *
 * Sends USER and, if the server demands a password (@c 331), PASS.
 * Empty @p user becomes `anonymous`; empty @p password becomes
 * `anonymous@` which most public mirrors accept.
 *
 * @param session  Open session.
 * @param user     Username or empty string.
 * @param password Password or empty string.
 * @return @c true when the server returns @c 230 (logged in).
 */
bool ftp_session_login(ftp_session *session, const char *user, const char *password);

/**
 * @brief Change the remote working directory via CWD.
 *
 * @param session Open and authenticated session.
 * @param path    Remote directory.
 * @return @c true on @c 250.
 */
bool ftp_session_change_directory(ftp_session *session, const char *path);

/**
 * @brief Request a passive-mode data connection.
 *
 * Sends PASV, parses the returned address and opens a second TCP socket
 * to the advertised host:port.
 *
 * @param session Open session.
 * @return Connected data-socket file descriptor, or -1 on failure.
 */
int ftp_session_open_passive(ftp_session *session);

/**
 * @brief List the current remote directory using an already-opened data socket.
 *
 * The caller is responsible for opening the data socket with
 * ::ftp_session_open_passive immediately beforehand. Entries are appended
 * to @p out_entries via @c realloc; the caller must @c free the array.
 *
 * @param session       Open session.
 * @param data_socket   Passive data socket returned by ::ftp_session_open_passive.
 * @param out_entries   Output pointer to an array of entries (malloc'd).
 * @param out_count     Number of entries written.
 * @return @c true on success.
 */
bool ftp_session_list(ftp_session *session,
                      int data_socket,
                      ftp_list_entry **out_entries,
                      size_t *out_count);

/**
 * @brief Download one remote file to @p local_path.
 *
 * Opens its own passive data connection, issues RETR and streams the body
 * to disk. Any existing local file is overwritten.
 *
 * @param session     Open session.
 * @param remote_name Remote filename inside the current directory.
 * @param local_path  Local destination path.
 * @return @c true when the server closes the data connection with @c 226.
 */
bool ftp_session_retrieve_file(ftp_session *session,
                               const char *remote_name,
                               const char *local_path);

/**
 * @brief Recursively mirror @p remote_dir into @p local_dir.
 *
 * Creates @p local_dir if it does not exist, then descends up to
 * @p max_depth levels downloading every regular file and recreating every
 * subdirectory. A @p max_depth of 0 means "only the top level, no recursion";
 * a negative value means "unlimited".
 *
 * @param session    Open and authenticated session.
 * @param remote_dir Absolute or CWD-relative remote directory.
 * @param local_dir  Local destination directory.
 * @param max_depth  Maximum recursion depth (see above).
 * @return @c true when every file and directory transferred successfully.
 */
bool ftp_session_mirror(ftp_session *session,
                        const char *remote_dir,
                        const char *local_dir,
                        int max_depth);

/**
 * @brief Send QUIT and close the control connection.
 *
 * Safe to call on an already-closed session.
 *
 * @param session Session to terminate.
 */
void ftp_session_quit(ftp_session *session);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FTP_CLIENT_H */
