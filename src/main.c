/**
 * @file main.c
 * @brief Command-line front-end for the FTP mirror library.
 *
 * Usage:
 * @code
 *   ftp_mirror [user@]host[/remote/path] local_dir [-n depth]
 * @endcode
 */
#include "ftp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/** Exit code returned on invalid command-line arguments. */
#define EXIT_USAGE 2

/**
 * @brief Print the usage banner.
 */
static void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage: %s [user@]host[/remote/path] local_dir [-n depth]\n"
            "  -n depth   maximum recursion depth (default: unlimited)\n",
            program_name);
}

/**
 * @brief Read a password from the controlling terminal without echoing.
 *
 * Falls back to reading the line with echo on when stdin is not a TTY.
 *
 * @param buffer      Destination buffer.
 * @param buffer_size Size of @p buffer.
 * @return @c true when at least one character was read successfully.
 */
static bool read_password_silently(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    struct termios previous;
    bool is_tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &previous) == 0;
    if (is_tty) {
        struct termios silent = previous;
        silent.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &silent);
    }

    bool ok = fgets(buffer, (int)buffer_size, stdin) != NULL;

    if (is_tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &previous);
        fputc('\n', stderr);
    }

    if (!ok) {
        return false;
    }
    size_t length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[length - 1] = '\0';
    }
    return true;
}

/**
 * @brief Parse @p argv into server URL, local dir and max depth.
 *
 * @return @c true on well-formed input.
 */
static bool parse_arguments(int argc,
                            char *argv[],
                            const char **server_url,
                            const char **local_dir,
                            int *max_depth)
{
    *server_url = NULL;
    *local_dir = NULL;
    *max_depth = -1;

    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                return false;
            }
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                return false;
            }
            *max_depth = (int)value;
        } else if (positional == 0) {
            *server_url = argv[i];
            positional++;
        } else if (positional == 1) {
            *local_dir = argv[i];
            positional++;
        } else {
            return false;
        }
    }
    return *server_url != NULL && *local_dir != NULL;
}

int main(int argc, char *argv[])
{
    const char *server_url = NULL;
    const char *local_dir = NULL;
    int max_depth = -1;

    if (!parse_arguments(argc, argv, &server_url, &local_dir, &max_depth)) {
        print_usage(argv[0]);
        return EXIT_USAGE;
    }

    ftp_url parsed;
    if (!ftp_parse_url(server_url, &parsed)) {
        fprintf(stderr, "Invalid server URL: %s\n", server_url);
        return EXIT_USAGE;
    }

    char password[128] = "";
    if (parsed.user[0] != '\0') {
        fprintf(stderr, "Password for %s@%s: ", parsed.user, parsed.host);
        fflush(stderr);
        if (!read_password_silently(password, sizeof(password))) {
            fprintf(stderr, "Failed to read password.\n");
            return EXIT_FAILURE;
        }
    }

    ftp_session session;
    ftp_session_init(&session);

    printf("Connecting to %s:%d...\n", parsed.host, FTP_DEFAULT_PORT);
    if (!ftp_session_connect(&session, parsed.host, FTP_DEFAULT_PORT)) {
        fprintf(stderr, "Failed to connect to %s.\n", parsed.host);
        return EXIT_FAILURE;
    }

    const char *login_name = parsed.user[0] != '\0' ? parsed.user : "anonymous";
    printf("Logging in as %s...\n", login_name);
    if (!ftp_session_login(&session, parsed.user, password)) {
        fprintf(stderr, "Login failed: %s", session.last_response);
        ftp_session_quit(&session);
        return EXIT_FAILURE;
    }

    printf("Mirroring %s -> %s (depth: %s)\n",
           parsed.path, local_dir,
           max_depth < 0 ? "unlimited" : "limited");
    bool mirror_ok = ftp_session_mirror(&session, parsed.path, local_dir, max_depth);

    ftp_session_quit(&session);

    if (!mirror_ok) {
        fprintf(stderr, "Mirror finished with errors.\n");
        return EXIT_FAILURE;
    }
    printf("Mirror finished successfully.\n");
    return EXIT_SUCCESS;
}
