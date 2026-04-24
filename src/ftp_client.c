/**
 * @file ftp_client.c
 * @brief Implementation of the FTP client declared in ftp_client.h.
 */
#include "ftp_client.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Internal constants and helpers.                                     */
/* ------------------------------------------------------------------ */

/** Minimum buffer required to hold a dotted-quad IPv4 literal + NUL. */
#define FTP_IPV4_BUFFER_SIZE 16

/** Initial capacity of the dynamic listing-entry array in ftp_session_list. */
#define FTP_LIST_INITIAL_CAPACITY 16

/** Chunk size used when streaming file bodies over the data connection. */
#define FTP_STREAM_CHUNK_SIZE 4096

/**
 * @brief Send the full contents of @p command over @p socket.
 * @return @c true when every byte was written.
 */
static bool send_all(int socket, const char *command)
{
    size_t remaining = strlen(command);
    const char *cursor = command;
    while (remaining > 0) {
        ssize_t written = send(socket, cursor, remaining, 0);
        if (written <= 0) {
            return false;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return true;
}

/**
 * @brief Check whether @p buffer ends with a complete FTP reply.
 *
 * A complete reply terminates with a line whose first four characters
 * are `NNN ` (three digits, space), signalling the end of a possibly
 * multi-line response per RFC 959 section 4.2.
 */
static bool response_is_complete(const char *buffer, size_t length)
{
    if (length < 5 || buffer[length - 1] != '\n') {
        return false;
    }
    size_t line_end = length - 1;
    if (line_end > 0 && buffer[line_end - 1] == '\r') {
        line_end--;
    }
    size_t line_start = line_end;
    while (line_start > 0 && buffer[line_start - 1] != '\n') {
        line_start--;
    }
    if (line_end - line_start < 4) {
        return false;
    }
    return isdigit((unsigned char)buffer[line_start])
        && isdigit((unsigned char)buffer[line_start + 1])
        && isdigit((unsigned char)buffer[line_start + 2])
        && buffer[line_start + 3] == ' ';
}

/**
 * @brief Read a complete FTP reply from @p socket into @p buffer.
 *
 * Loops over @c recv until ::response_is_complete accepts the accumulated
 * bytes or the buffer fills up.
 */
static bool read_control_response(int socket, char *buffer, size_t buffer_size)
{
    size_t total = 0;
    while (total + 1 < buffer_size) {
        ssize_t received = recv(socket, buffer + total, buffer_size - 1 - total, 0);
        if (received < 0) {
            return false;
        }
        if (received == 0) {
            break;
        }
        total += (size_t)received;
        buffer[total] = '\0';
        if (response_is_complete(buffer, total)) {
            return true;
        }
    }
    buffer[total] = '\0';
    return total > 0;
}

/**
 * @brief Send a command and read the response into @p session->last_response.
 */
static bool exchange(ftp_session *session, const char *command)
{
    if (!send_all(session->control_socket, command)) {
        return false;
    }
    return read_control_response(session->control_socket,
                                 session->last_response,
                                 sizeof(session->last_response));
}

/**
 * @brief Open an IPv4 TCP connection to @p host on @p port.
 */
static int tcp_connect(const char *host, int port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%d", port);

    struct addrinfo *resolved = NULL;
    if (getaddrinfo(host, port_text, &hints, &resolved) != 0 || resolved == NULL) {
        return -1;
    }

    int sock = socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(resolved);
        return -1;
    }

    if (connect(sock, resolved->ai_addr, resolved->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(resolved);
        return -1;
    }

    freeaddrinfo(resolved);
    return sock;
}

/* ------------------------------------------------------------------ */
/* Pure parsing helpers.                                               */
/* ------------------------------------------------------------------ */

bool ftp_parse_url(const char *url, ftp_url *out)
{
    if (url == NULL || out == NULL || url[0] == '\0') {
        return false;
    }

    ftp_url parsed;
    parsed.user[0] = '\0';
    parsed.host[0] = '\0';
    strcpy(parsed.path, "/");

    const char *host_start = url;
    const char *at_sign = strchr(url, '@');
    if (at_sign != NULL) {
        size_t user_length = (size_t)(at_sign - url);
        if (user_length == 0 || user_length >= sizeof(parsed.user)) {
            return false;
        }
        memcpy(parsed.user, url, user_length);
        parsed.user[user_length] = '\0';
        host_start = at_sign + 1;
    }

    const char *path_start = strchr(host_start, '/');
    size_t host_length;
    if (path_start != NULL) {
        host_length = (size_t)(path_start - host_start);
        size_t path_length = strlen(path_start);
        if (path_length >= sizeof(parsed.path)) {
            return false;
        }
        memcpy(parsed.path, path_start, path_length + 1);
    } else {
        host_length = strlen(host_start);
    }

    if (host_length == 0 || host_length >= sizeof(parsed.host)) {
        return false;
    }
    memcpy(parsed.host, host_start, host_length);
    parsed.host[host_length] = '\0';

    *out = parsed;
    return true;
}

int ftp_parse_response_code(const char *response)
{
    if (response == NULL) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)response[i])) {
            return -1;
        }
    }
    return (response[0] - '0') * 100
         + (response[1] - '0') * 10
         + (response[2] - '0');
}

bool ftp_parse_pasv_response(const char *response,
                             char *ip_out,
                             size_t ip_size,
                             int *port_out)
{
    if (response == NULL || ip_out == NULL || port_out == NULL
        || ip_size < FTP_IPV4_BUFFER_SIZE) {
        return false;
    }

    const char *open_paren = strchr(response, '(');
    if (open_paren == NULL) {
        return false;
    }

    int h1, h2, h3, h4, p1, p2;
    if (sscanf(open_paren, "(%d,%d,%d,%d,%d,%d)",
               &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return false;
    }

    const int parts[] = {h1, h2, h3, h4, p1, p2};
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if (parts[i] < 0 || parts[i] > 255) {
            return false;
        }
    }

    int written = snprintf(ip_out, ip_size, "%d.%d.%d.%d", h1, h2, h3, h4);
    if (written < 0 || (size_t)written >= ip_size) {
        return false;
    }

    *port_out = p1 * 256 + p2;
    return true;
}

/**
 * @brief Advance @p cursor past one whitespace-delimited field.
 * @return Pointer to the first non-whitespace byte of the next field.
 */
static const char *skip_one_field(const char *cursor)
{
    while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        cursor++;
    }
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

bool ftp_parse_list_line(const char *line, ftp_list_entry *out)
{
    if (line == NULL || out == NULL || line[0] == '\0') {
        return false;
    }

    ftp_entry_kind kind;
    switch (line[0]) {
        case '-': kind = FTP_ENTRY_FILE;      break;
        case 'd': kind = FTP_ENTRY_DIRECTORY; break;
        case 'l': kind = FTP_ENTRY_SYMLINK;   break;
        default:  kind = FTP_ENTRY_OTHER;     break;
    }

    const char *cursor = line;
    for (int field = 0; field < 4; field++) {
        cursor = skip_one_field(cursor);
        if (*cursor == '\0') {
            return false;
        }
    }
    long long size = strtoll(cursor, NULL, 10);

    for (int field = 0; field < 4; field++) {
        cursor = skip_one_field(cursor);
        if (*cursor == '\0') {
            return false;
        }
    }

    size_t name_length = 0;
    while (cursor[name_length] != '\0'
           && cursor[name_length] != '\r'
           && cursor[name_length] != '\n') {
        name_length++;
    }

    /* Symlinks include " -> target"; trim it. */
    if (kind == FTP_ENTRY_SYMLINK) {
        const char *arrow = strstr(cursor, " -> ");
        if (arrow != NULL && (size_t)(arrow - cursor) < name_length) {
            name_length = (size_t)(arrow - cursor);
        }
    }

    if (name_length == 0 || name_length >= sizeof(out->name)) {
        return false;
    }

    memcpy(out->name, cursor, name_length);
    out->name[name_length] = '\0';

    if (strcmp(out->name, ".") == 0 || strcmp(out->name, "..") == 0) {
        return false;
    }

    out->kind = kind;
    out->size = (kind == FTP_ENTRY_DIRECTORY) ? 0 : size;
    return true;
}

bool ftp_join_path(const char *base, const char *name, char *out, size_t out_size)
{
    if (base == NULL || name == NULL || out == NULL || out_size == 0) {
        return false;
    }

    size_t base_length = strlen(base);
    while (base_length > 1 && base[base_length - 1] == '/') {
        base_length--;
    }

    while (*name == '/') {
        name++;
    }

    int written;
    if (base_length == 0) {
        written = snprintf(out, out_size, "%s", name);
    } else if (base_length == 1 && base[0] == '/') {
        written = snprintf(out, out_size, "/%s", name);
    } else {
        written = snprintf(out, out_size, "%.*s/%s", (int)base_length, base, name);
    }

    return written >= 0 && (size_t)written < out_size;
}

/* ------------------------------------------------------------------ */
/* Session lifecycle.                                                  */
/* ------------------------------------------------------------------ */

void ftp_session_init(ftp_session *session)
{
    if (session == NULL) {
        return;
    }
    session->control_socket = -1;
    session->last_response[0] = '\0';
}

bool ftp_session_connect(ftp_session *session, const char *host, int port)
{
    if (session == NULL || host == NULL) {
        return false;
    }

    int sock = tcp_connect(host, port);
    if (sock < 0) {
        return false;
    }
    session->control_socket = sock;

    if (!read_control_response(sock, session->last_response,
                               sizeof(session->last_response))) {
        close(sock);
        session->control_socket = -1;
        return false;
    }

    if (ftp_parse_response_code(session->last_response) != 220) {
        close(sock);
        session->control_socket = -1;
        return false;
    }
    return true;
}

bool ftp_session_login(ftp_session *session, const char *user, const char *password)
{
    if (session == NULL || session->control_socket < 0) {
        return false;
    }

    const char *effective_user = (user != NULL && user[0] != '\0') ? user : "anonymous";
    const char *effective_password =
        (password != NULL && password[0] != '\0') ? password : "anonymous@";

    char command[FTP_MAX_USER + 16];
    snprintf(command, sizeof(command), "USER %s\r\n", effective_user);
    if (!exchange(session, command)) {
        return false;
    }

    int code = ftp_parse_response_code(session->last_response);
    if (code == 230) {
        return true;
    }
    if (code != 331) {
        return false;
    }

    snprintf(command, sizeof(command), "PASS %s\r\n", effective_password);
    if (!exchange(session, command)) {
        return false;
    }
    return ftp_parse_response_code(session->last_response) == 230;
}

bool ftp_session_change_directory(ftp_session *session, const char *path)
{
    if (session == NULL || session->control_socket < 0 || path == NULL) {
        return false;
    }

    char command[FTP_MAX_PATH + 16];
    int written = snprintf(command, sizeof(command), "CWD %s\r\n", path);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return false;
    }
    if (!exchange(session, command)) {
        return false;
    }
    return ftp_parse_response_code(session->last_response) == 250;
}

int ftp_session_open_passive(ftp_session *session)
{
    if (session == NULL || session->control_socket < 0) {
        return -1;
    }

    if (!exchange(session, "PASV\r\n")) {
        return -1;
    }
    if (ftp_parse_response_code(session->last_response) != 227) {
        return -1;
    }

    char ip[FTP_IPV4_BUFFER_SIZE];
    int port = 0;
    if (!ftp_parse_pasv_response(session->last_response, ip, sizeof(ip), &port)) {
        return -1;
    }
    return tcp_connect(ip, port);
}

/**
 * @brief Read @p data_socket until EOF into a freshly-allocated buffer.
 *
 * @param data_socket   Data connection file descriptor.
 * @param out_buffer    Output buffer (must be freed by caller).
 * @param out_length    Output length excluding the terminating NUL.
 * @return @c true on success.
 */
static bool drain_data_socket(int data_socket, char **out_buffer, size_t *out_length)
{
    size_t capacity = FTP_STREAM_CHUNK_SIZE;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        return false;
    }

    while (true) {
        if (length + FTP_STREAM_CHUNK_SIZE + 1 > capacity) {
            size_t new_capacity = capacity * 2;
            char *grown = (char *)realloc(buffer, new_capacity);
            if (grown == NULL) {
                free(buffer);
                return false;
            }
            buffer = grown;
            capacity = new_capacity;
        }

        ssize_t received = recv(data_socket, buffer + length, FTP_STREAM_CHUNK_SIZE, 0);
        if (received < 0) {
            free(buffer);
            return false;
        }
        if (received == 0) {
            break;
        }
        length += (size_t)received;
    }

    buffer[length] = '\0';
    *out_buffer = buffer;
    *out_length = length;
    return true;
}

bool ftp_session_list(ftp_session *session,
                      int data_socket,
                      ftp_list_entry **out_entries,
                      size_t *out_count)
{
    if (session == NULL || data_socket < 0
        || out_entries == NULL || out_count == NULL) {
        return false;
    }

    *out_entries = NULL;
    *out_count = 0;

    if (!send_all(session->control_socket, "LIST\r\n")) {
        close(data_socket);
        return false;
    }

    if (!read_control_response(session->control_socket,
                               session->last_response,
                               sizeof(session->last_response))) {
        close(data_socket);
        return false;
    }
    int preliminary = ftp_parse_response_code(session->last_response);
    if (preliminary != 150 && preliminary != 125) {
        close(data_socket);
        return false;
    }

    char *listing = NULL;
    size_t listing_length = 0;
    bool drained = drain_data_socket(data_socket, &listing, &listing_length);
    close(data_socket);
    if (!drained) {
        return false;
    }

    if (!read_control_response(session->control_socket,
                               session->last_response,
                               sizeof(session->last_response))
        || ftp_parse_response_code(session->last_response) != 226) {
        free(listing);
        return false;
    }

    size_t capacity = FTP_LIST_INITIAL_CAPACITY;
    ftp_list_entry *entries =
        (ftp_list_entry *)malloc(capacity * sizeof(ftp_list_entry));
    if (entries == NULL) {
        free(listing);
        return false;
    }
    size_t count = 0;

    char *save_ptr = NULL;
    for (char *line = strtok_r(listing, "\n", &save_ptr);
         line != NULL;
         line = strtok_r(NULL, "\n", &save_ptr)) {
        ftp_list_entry entry;
        if (!ftp_parse_list_line(line, &entry)) {
            continue;
        }
        if (count == capacity) {
            size_t new_capacity = capacity * 2;
            ftp_list_entry *grown = (ftp_list_entry *)realloc(
                entries, new_capacity * sizeof(ftp_list_entry));
            if (grown == NULL) {
                free(entries);
                free(listing);
                return false;
            }
            entries = grown;
            capacity = new_capacity;
        }
        entries[count++] = entry;
    }

    free(listing);
    *out_entries = entries;
    *out_count = count;
    return true;
}

bool ftp_session_retrieve_file(ftp_session *session,
                               const char *remote_name,
                               const char *local_path)
{
    if (session == NULL || remote_name == NULL || local_path == NULL) {
        return false;
    }

    int data_socket = ftp_session_open_passive(session);
    if (data_socket < 0) {
        return false;
    }

    char command[FTP_MAX_PATH + 16];
    int written = snprintf(command, sizeof(command), "RETR %s\r\n", remote_name);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        close(data_socket);
        return false;
    }
    if (!send_all(session->control_socket, command)) {
        close(data_socket);
        return false;
    }

    if (!read_control_response(session->control_socket,
                               session->last_response,
                               sizeof(session->last_response))) {
        close(data_socket);
        return false;
    }
    int preliminary = ftp_parse_response_code(session->last_response);
    if (preliminary != 150 && preliminary != 125) {
        close(data_socket);
        return false;
    }

    FILE *output = fopen(local_path, "wb");
    if (output == NULL) {
        close(data_socket);
        return false;
    }

    char chunk[FTP_STREAM_CHUNK_SIZE];
    bool transfer_ok = true;
    while (true) {
        ssize_t received = recv(data_socket, chunk, sizeof(chunk), 0);
        if (received < 0) {
            transfer_ok = false;
            break;
        }
        if (received == 0) {
            break;
        }
        if (fwrite(chunk, 1, (size_t)received, output) != (size_t)received) {
            transfer_ok = false;
            break;
        }
    }

    fclose(output);
    close(data_socket);

    if (!transfer_ok) {
        return false;
    }

    if (!read_control_response(session->control_socket,
                               session->last_response,
                               sizeof(session->last_response))) {
        return false;
    }
    return ftp_parse_response_code(session->last_response) == 226;
}

/**
 * @brief Create @p path with mode 0755, tolerating EEXIST.
 */
static bool ensure_local_directory(const char *path)
{
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

bool ftp_session_mirror(ftp_session *session,
                        const char *remote_dir,
                        const char *local_dir,
                        int max_depth)
{
    if (session == NULL || remote_dir == NULL || local_dir == NULL) {
        return false;
    }

    if (!ensure_local_directory(local_dir)) {
        return false;
    }

    if (!ftp_session_change_directory(session, remote_dir)) {
        return false;
    }

    int data_socket = ftp_session_open_passive(session);
    if (data_socket < 0) {
        return false;
    }

    ftp_list_entry *entries = NULL;
    size_t entry_count = 0;
    if (!ftp_session_list(session, data_socket, &entries, &entry_count)) {
        return false;
    }

    bool overall_ok = true;

    for (size_t i = 0; i < entry_count; i++) {
        const ftp_list_entry *entry = &entries[i];
        char local_target[FTP_MAX_PATH];
        if (!ftp_join_path(local_dir, entry->name, local_target, sizeof(local_target))) {
            overall_ok = false;
            continue;
        }

        if (entry->kind == FTP_ENTRY_DIRECTORY) {
            if (max_depth == 0) {
                continue;
            }
            char remote_target[FTP_MAX_PATH];
            if (!ftp_join_path(remote_dir, entry->name, remote_target,
                               sizeof(remote_target))) {
                overall_ok = false;
                continue;
            }
            int child_depth = (max_depth < 0) ? max_depth : max_depth - 1;
            if (!ftp_session_mirror(session, remote_target, local_target, child_depth)) {
                overall_ok = false;
            }
            /* Restore CWD so the loop continues at the correct level. */
            if (!ftp_session_change_directory(session, remote_dir)) {
                overall_ok = false;
                break;
            }
        } else if (entry->kind == FTP_ENTRY_FILE) {
            if (!ftp_session_retrieve_file(session, entry->name, local_target)) {
                overall_ok = false;
            }
        }
        /* Symlinks and other entries are skipped by design. */
    }

    free(entries);
    return overall_ok;
}

void ftp_session_quit(ftp_session *session)
{
    if (session == NULL || session->control_socket < 0) {
        return;
    }
    (void)exchange(session, "QUIT\r\n");
    close(session->control_socket);
    session->control_socket = -1;
}
