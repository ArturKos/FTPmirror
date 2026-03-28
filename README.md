# FTPmirror

FTP directory mirroring tool that syncs a remote FTP directory to local storage. Supports passive mode transfers, recursive directory downloads, and automatic directory creation.

## Features

- Raw socket-based FTP protocol implementation
- Passive mode (PASV) for firewall-friendly data connections
- Recursive directory listing and download
- Anonymous login support

## Build & Run

```bash
gcc mirror.c -o mirror
./mirror ftp.example.com/pub/directory ./download -1
```

Built and tested on Ubuntu Linux.
