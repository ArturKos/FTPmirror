# FTPmirror

![C](https://img.shields.io/badge/C-11-00599C?style=flat&logo=c&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat&logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-%E2%89%A53.14-064F8C?style=flat&logo=cmake&logoColor=white)
![GoogleTest](https://img.shields.io/badge/tests-GoogleTest-4285F4?style=flat&logo=google&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey?style=flat&logo=linux&logoColor=white)
![License](https://img.shields.io/badge/license-MIT-blue?style=flat)

FTP directory mirroring tool that synchronises a remote FTP directory tree to local storage. Built directly on BSD sockets with a hand-written FTP protocol implementation covering passive-mode transfers, anonymous and user/password authentication, and recursive downloads with configurable depth.

## Features

- **Raw socket FTP client** — speaks RFC 959 over plain BSD sockets (`socket`, `connect`, `send`, `recv`) with no external FTP library.
- **Passive mode (PASV)** — parses the server's `227` reply, opens a separate data connection per transfer.
- **Recursive mirroring** — downloads every regular file and recreates every subdirectory locally using `LIST` + `RETR`.
- **Configurable recursion depth** — `-n <depth>` limits how deep the mirror descends; omit the flag for unlimited.
- **Anonymous and user/password login** — `[user@]host[/path]` URL syntax; the password is read from the terminal with echo disabled.
- **Robust response handling** — reads multi-line FTP replies correctly, validates numeric reply codes, handles hostname resolution via `getaddrinfo`.
- **Documented public API** — every exported function, constant and type carries Doxygen comments in `include/ftp_client.h`.
- **Google Test suite** — 31 unit tests covering the URL parser, PASV parser, response code parser, LIST-line parser and path joiner.

## Dependencies

| Dependency | Purpose |
|---|---|
| GCC / Clang with C11 + C++17 | Compilers for the library, CLI and tests |
| CMake ≥ 3.14 | Build-system generator |
| POSIX sockets (`arpa/inet.h`, `netinet/in.h`, `netdb.h`) | Network I/O |
| GoogleTest (system package or auto-fetched) | Unit tests |
| Doxygen *(optional)* | API documentation generation |

On Debian/Ubuntu:
```bash
sudo apt install build-essential cmake libgtest-dev doxygen
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

Produced artefacts:

| Target | Path | Purpose |
|---|---|---|
| `ftp_client` | `build/libftp_client.a` | Static library with the FTP logic |
| `ftp_mirror` | `build/ftp_mirror` | Command-line mirror tool |
| `ftp_client_tests` | `build/ftp_client_tests` | Google Test binary |

## Run the tests

```bash
cd build && ctest --output-on-failure
# or directly:
./build/ftp_client_tests
```

Expected output:
```
[==========] 31 tests from 5 test suites ran.
[  PASSED  ] 31 tests.
```

## Usage

```bash
./build/ftp_mirror [user@]host[/remote/path] local_dir [-n depth]
```

Arguments:
- `host` — FTP server hostname or IPv4 literal.
- `/remote/path` — optional path on the server (defaults to `/`).
- `user@` — optional username; omit for anonymous login.
- `local_dir` — local destination directory (created if it does not exist).
- `-n depth` — optional maximum recursion depth (`0` = top level only, negative or omitted = unlimited).

Examples:
```bash
# Anonymous, unlimited recursion
./build/ftp_mirror ftp.freebsd.org/pub/FreeBSD/doc ./mirror

# Anonymous, top level only
./build/ftp_mirror ftp.freebsd.org/pub/FreeBSD ./mirror -n 0

# Authenticated (password prompted interactively)
./build/ftp_mirror alice@ftp.example.com/home/alice ./backup -n 3
```

## Generate API documentation

If Doxygen is installed, CMake adds a `docs` target:
```bash
cmake --build build --target docs
xdg-open build/docs/html/index.html
```

## Project structure

```
FTPmirror/
├── CMakeLists.txt           # Build configuration (library, CLI, tests, docs)
├── include/
│   └── ftp_client.h         # Public API with Doxygen documentation
├── src/
│   ├── ftp_client.c         # FTP protocol + mirror implementation
│   └── main.c               # CLI front-end
├── tests/
│   └── test_ftp_client.cc   # Google Test suite for pure helpers
├── .gitignore
└── README.md
```

## Public API overview

All declarations live in `include/ftp_client.h`. The API splits into two groups.

**Pure parsing helpers** — no I/O, fully tested:
- `ftp_parse_url` — `[user@]host[/path]` → `ftp_url`
- `ftp_parse_response_code` — 3-digit reply code from a raw response
- `ftp_parse_pasv_response` — `227 (h1,h2,h3,h4,p1,p2)` → IPv4 + port
- `ftp_parse_list_line` — one UNIX `LIST` line → `ftp_list_entry`
- `ftp_join_path` — slash-safe path concatenation

**Session-level networking**:
- `ftp_session_init` / `ftp_session_connect` / `ftp_session_quit`
- `ftp_session_login` — USER/PASS with anonymous fallback
- `ftp_session_change_directory` — CWD
- `ftp_session_open_passive` — PASV + data connect
- `ftp_session_list` — LIST + parse into an array of entries
- `ftp_session_retrieve_file` — RETR one file to disk
- `ftp_session_mirror` — recursive LIST + RETR with depth cap

## Protocol flow

```
Client                                       Server
  │ ── TCP connect host:21 ─────────────────>│
  │ <─────────────────────── 220 Welcome ────│
  │ ── USER <name> ─────────────────────────>│
  │ <──────────────── 331 / 230 ─────────────│
  │ ── PASS <secret>  (if 331) ─────────────>│
  │ <────────────────────────── 230 ─────────│
  │ ── CWD /remote/path ────────────────────>│
  │ <────────────────────────── 250 ─────────│
  │ ── PASV ────────────────────────────────>│
  │ <───── 227 Entering Passive (...,p1,p2) ─│
  │ ── TCP connect data host:p1*256+p2 ─────>│
  │ ── LIST ────────────────────────────────>│
  │ <────────────────────────── 150 ─────────│
  │ <════ directory listing over data sock ══│
  │ <────────────────────────── 226 ─────────│
  │   (repeat PASV + RETR/LIST per entry)
  │ ── QUIT ────────────────────────────────>│
  │ <────────────────────────── 221 ─────────│
```

## Design notes

- **Header is declarations only.** `ftp_client.h` carries no function bodies, uses include guards and the `extern "C"` wrapper so C++ tests can consume it without modification.
- **Bounded buffers.** No `strcat`-into-fixed-size; every format operation uses `snprintf` and checks for truncation.
- **Multi-line replies.** `read_control_response` keeps reading until a line matches `NNN ` (three digits followed by space), per RFC 959 §4.2.
- **Separate concerns.** Pure parsers are decoupled from networking so they can be unit-tested without a server; `ftp_session_*` functions do not know about the CLI.
- **Safe defaults.** Missing username → `anonymous`, missing password → `anonymous@`, missing remote path → `/`.

## Limitations

- IPv4 only (no EPSV / EPRT support).
- UNIX-style `LIST` output only (no MLSD, no MS-DOS listings).
- Binary transfer assumed (no explicit TYPE I — most servers default to it; add `TYPE I` if your server needs it).
- No TLS (no `AUTH TLS`). Do not use over untrusted networks with a real password.
- Device nodes in listings are recognised as "other" and skipped, but their names may be parsed imperfectly — by design, because they cannot be mirrored anyway.
