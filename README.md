# Networking Practice

C++ socket programming exercises, built up from scratch using POSIX BSD sockets. Each project lives in its own directory and is self-contained.
The aim of this practice is to learn enough to be able to apply TCP/IP networking concepts in a larger limit order book based project.

## Projects

| Directory | Description |
|-----------|-------------|
| `echo1/`  | Simple TCP echo server + client. Client sends one message, server sends it back. |
| `echo2/`  | Simple TCP echo server + client. Client sends one message following a specific protocol (first 2 bytes denote message size). server sends only the message back after properly parsing the input data stream |

## Common Build Pattern

Each project compiles with g++ directly:

```bash
g++ server.cpp -o bin/server
g++ client.cpp -o bin/client
```

Run the server first, then the client in a separate terminal:

```bash
./bin/server        # blocks, waiting for connections
./bin/client        # connect, send a message, receive the echo
```

## Concepts Covered

- BSD socket API (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`)
- `getaddrinfo` for protocol-agnostic address resolution
- IPv4/IPv6 dual-stack with `AF_UNSPEC`
- TCP connection lifecycle (three-way handshake, graceful close)
- File descriptors as the abstraction for network I/O
