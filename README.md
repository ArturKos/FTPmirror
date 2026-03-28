# FTPmirror

![C](https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey?style=flat&logo=linux&logoColor=white)
![License](https://img.shields.io/badge/license-MIT-blue?style=flat)

FTP directory mirroring tool that synchronizes a remote FTP directory tree to local storage. Built entirely on raw BSD sockets with a hand-rolled FTP protocol implementation, supporting passive mode transfers and recursive directory downloads.

## Features

- **Raw socket FTP implementation** -- connects to FTP servers on port 21 using the POSIX socket API (`socket`, `connect`, `send`, `recv`) without any external FTP library
- **Passive mode (PASV)** -- negotiates a passive data connection by parsing the server's `227` response to extract the data channel IP and port, enabling firewall-friendly transfers
- **Recursive directory mirroring** -- lists remote directories via the `LIST` command and recreates the directory structure locally with automatic `mkdir` calls
- **Hostname resolution** -- resolves FTP server hostnames through `gethostbyname` before connecting
- **Configurable nesting depth** -- optional `-n` flag controls how many directory levels deep the mirror descends
- **Anonymous login** -- authenticates with `USER anonymous` / `PASS` for public FTP servers

## Dependencies

| Dependency | Purpose |
|---|---|
| GCC | C compiler |
| POSIX sockets (`arpa/inet.h`, `netinet/in.h`) | Network communication |
| `netdb.h` | DNS hostname resolution |

## Build and Run

```bash
gcc mirror.c -o mirror
./mirror user@ftp.server/remote/dir ./local_dir
./mirror user@ftp.server/remote/dir ./local_dir -n   # limit nesting depth
```

Example with a public FTP server:

```bash
./mirror ftp.man.szczecin.pl/pub/gnu ./download -1
```

## Project Structure

```
FTPmirror/
  mirror.c       # Main entry point -- argument parsing, connection orchestration
  mirror.h       # FTP protocol functions -- init, login, PASV, CWD, LIST, logout
  build           # Build and run helper script
```

## How It Works

1. The user-supplied URL is split into hostname and remote directory path.
2. A TCP socket connects to port 21 of the resolved host.
3. The client authenticates (anonymous) and issues `PASV` to open a data channel.
4. `CWD` navigates to the target remote directory.
5. `LIST` retrieves the directory listing over the data connection.
6. Files and subdirectories are downloaded and recreated locally under the specified output path.
