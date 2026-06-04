# echo1 — Simple TCP Echo Server

A minimal client/server pair. The client asks you to type a message, sends it to the server over TCP, and the server sends the exact bytes back. One message per connection; the connection closes after the echo.

## Mental mind map
Sockets are just "files" somewhere on your machine. You write bytes to your sockets and also receive bytes on your sockets. These sockets are identified by integers, called file descriptors.

## High-Level Flow

```
CLIENT                              SERVER
------                              ------
getaddrinfo("localhost", "8080")    getaddrinfo(NULL, "8080")   <- resolve addresses
socket()                            socket()                     <- get a file descriptor
connect()          ── SYN ──>       listen()                     <- TCP handshake
                   <── SYN-ACK ──   accept()                     <- blocks until client arrives
                   ── ACK ──>
send(message)      ── data ──>      recv()                       <- read bytes off the wire
recv(echo)         <── data ──      send()                       <- write same bytes back
close()            ── FIN ──>       close(clientfd)              <- tear down this connection
```

---

## Headers

```cpp
#include <iostream>        // std::cout, std::cerr, std::cin
#include <cstring>         // memset(), bzero()
#include <string>          // std::string  (client only)
#include <sys/types.h>     // typedefs: socklen_t, ssize_t, etc.
#include <sys/socket.h>    // socket(), bind(), listen(), accept(), connect(), send(), recv()
#include <netdb.h>         // getaddrinfo(), freeaddrinfo(), struct addrinfo
#include <arpa/inet.h>     // inet_ntop(), htons()/ntohs() — byte-order helpers
#include <netinet/in.h>    // struct sockaddr_in, struct sockaddr_in6, IPPROTO_TCP
#include <unistd.h>        // close()
```

---

## Key Data Structures

### The `sockaddr` Family

These form a C-style polymorphism hierarchy. `sockaddr` is the generic base that all syscalls
accept. The concrete types hold the actual address data. You fill in a concrete type, then cast
to `(struct sockaddr *)` when calling syscalls.

```
sockaddr              <- generic base, used as the "interface" in all syscalls
├── sockaddr_in       <- concrete IPv4
├── sockaddr_in6      <- concrete IPv6
└── sockaddr_un       <- concrete Unix domain socket (filepath, no IP at all)

sockaddr_storage      <- a buffer big enough to hold any of the above
```

**Why are they called "socket address"?**
It stores not just the IP, but the IP + port together (the full identity of one endpoint of a
connection). The name is deliberately broader: Unix domain sockets use a filesystem path
as their "address," with no IP at all.

**How the polymorphism works:**
Every struct in this family starts with `sa_family` as its very first field. The kernel always
reads that field first to know how to interpret the rest of the bytes. You cast between types,
and it works because the discriminant field is always in the same position.

---

#### `struct sockaddr` — the generic interface

```c
struct sockaddr {
    sa_family_t sa_family;   // which family: AF_INET, AF_INET6, AF_UNIX, ...
    char        sa_data[14]; // raw address bytes — opaque, never write to this directly
};
// total: 16 bytes
```

You never fill this in directly. It exists purely so syscalls like `bind()`, `connect()`,
`accept()` can have one signature that works for all address families:

```c
int bind(int sockfd, struct sockaddr *addr, socklen_t addrlen);
//                   ^^^^^^^^^^^^^^^^
//                   takes the base type — you cast your concrete type to this
```

---

#### `struct sockaddr_in` — IPv4 concrete type

```c
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET
    in_port_t      sin_port;     // port in network byte order (big-endian)
    struct in_addr sin_addr;     // 4-byte IPv4 address
    char           sin_zero[8];  // padding to match sockaddr's 16-byte size
};
// total: 16 bytes — same as sockaddr, so the cast is safe
```

The `sin_zero` padding exists purely to make the sizes match so the cast to `sockaddr *` is
safe. When you use `getaddrinfo`, it fills this in for you — you don't construct it manually.

---

#### `struct sockaddr_in6` — IPv6 concrete type

```c
struct sockaddr_in6 {
    sa_family_t     sin6_family;    // AF_INET6
    in_port_t       sin6_port;      // port in network byte order
    uint32_t        sin6_flowinfo;  // flow info (usually 0)
    struct in6_addr sin6_addr;      // 16-byte IPv6 address
    uint32_t        sin6_scope_id;
};
// total: 28 bytes — larger because IPv6 addresses are 128-bit vs 32-bit
```

---

#### `struct sockaddr_storage` — catch-all buffer

```c
struct sockaddr_storage {
    sa_family_t ss_family;  // readable without casting — tells you which type is inside
    // ... opaque padding large enough to hold any sockaddr_* type
};
// total: typically 128 bytes
```

Use this when you don't know the address family ahead of time (e.g., `accept()` with
`AF_UNSPEC`). After the call, read `ss_family` to know which concrete type to cast to:

```c
struct sockaddr_storage their_addr;
accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

if (their_addr.ss_family == AF_INET) {
    struct sockaddr_in *s = (struct sockaddr_in *)&their_addr;
    // s->sin_addr is the IPv4 address
} else {
    struct sockaddr_in6 *s = (struct sockaddr_in6 *)&their_addr;
    // s->sin6_addr is the IPv6 address
}
```

---

### `struct addrinfo`

Used both as input (hints) and output (result linked list) for `getaddrinfo`.

```c
struct addrinfo {
    int              ai_flags;      // behavior flags: AI_PASSIVE, AI_CANONNAME, etc.
    int              ai_family;     // AF_INET (IPv4), AF_INET6 (IPv6), AF_UNSPEC (either)
    int              ai_socktype;   // SOCK_STREAM (TCP), SOCK_DGRAM (UDP)
    int              ai_protocol;   // IPPROTO_TCP, IPPROTO_UDP, or 0 to auto-select
    socklen_t        ai_addrlen;    // byte-length of the ai_addr field below
    struct sockaddr *ai_addr;       // points to a sockaddr_in or sockaddr_in6
    char            *ai_canonname;  // canonical hostname string (only if AI_CANONNAME set)
    struct addrinfo *ai_next;       // next node in linked list (NULL = end)
};
```

`getaddrinfo` can return multiple results (e.g., one IPv4 and one IPv6). In this project we
just use `res` (the first result) directly. A production server would iterate the list and
try each until `bind`/`connect` succeeds.

---

## `server.cpp` — Line by Line

```cpp
#define PORT "8080"
```
Port number as a **string**. `getaddrinfo` takes a service name or port string, not an integer. This is the port number on the server at which we want to communicate. 

```cpp
#define BACKLOG 10
```
Maximum number of incoming connections that can sit in the kernel's queue waiting for `accept()`
to be called. If the queue is full, new connection attempts are refused. 10 is a typical small
value for a single-threaded server. 

---

```cpp
struct sockaddr_storage their_addr;
socklen_t addr_size;
int sockfd, clientfd;
struct addrinfo hints, *res;
```

- `their_addr` — this is a buffer to hold the incoming client's address (IP + Port). It is filled by `accept()`
- `addr_size` — the size of `their_addr`; passed to `accept()` so it knows how much space it has.
- `sockfd` — the **listening** socket. Stays open for the lifetime of the server.
- `clientfd` — a **per-connection** socket returned by `accept()`. Each new client gets its own fd.
- `hints` — we fill this in to tell `getaddrinfo` what kind of address we want.
- `res` — `getaddrinfo` writes the result(s) here; we read from it to set up the socket.

---

```cpp
memset(&hints, 0, sizeof(hints));
```
Zero out every byte of `hints`. Required before setting specific fields — uninitialized padding
bytes inside the struct could confuse `getaddrinfo` if left as garbage.

```cpp
hints.ai_family   = AF_UNSPEC;
```
Accept either IPv4 or IPv6. `AF_INET` would force IPv4 only; `AF_INET6` IPv6 only.

```cpp
hints.ai_socktype = SOCK_STREAM;
```
We want a stream socket (TCP). `SOCK_DGRAM` would give us UDP.

```cpp
hints.ai_flags    = AI_PASSIVE;
```
Because we pass `NULL` as the hostname below, `AI_PASSIVE` tells `getaddrinfo` to fill in
`INADDR_ANY` (for IPv4) or `IN6ADDR_ANY` (for IPv6) — meaning "bind to all network interfaces
on this machine." Without this flag, `NULL` hostname resolves to the loopback address instead.

---

```cpp
if((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0){ ... }
```
Here, we pass hints to hostname + service (in this case NULL, i.e. the machine itself), which lets us filter out the types of ip addresses we want.
| Parameter | Value | Meaning |
|-----------|-------|---------|
| `node`    | `NULL` | Hostname to look up. NULL + AI_PASSIVE = any local interface. |
| `service` | `PORT` | Port number as a string (`"8080"`). |
| `hints`   | `&hints` | Our filter struct describing what we want. |
| `res`     | `&res` | Output: pointer to linked list of `addrinfo` results. Caller must `freeaddrinfo(res)`. |
| return    | 0 on success | Non-zero is an error code (not errno; use `gai_strerror()` to print). |

---

```cpp
sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
```

Creates a socket and returns a **file descriptor** — just an integer the kernel uses to track
the socket. No connection yet; this is just allocating the endpoint.

| Parameter    | Value                | Meaning |
|--------------|----------------------|---------|
| `domain`     | `res->ai_family`     | Address family: AF_INET or AF_INET6. |
| `type`       | `res->ai_socktype`   | SOCK_STREAM = TCP. |
| `protocol`   | `res->ai_protocol`   | 0 or IPPROTO_TCP (auto-selected from the above two). |
| return       | file descriptor ≥ 0  | -1 on error; check `errno`. |

---

```cpp
if((rv = bind(sockfd, res->ai_addr, res->ai_addrlen)) == -1){ ... }
```

Assigns the local address (IP + port) to the socket. After this call, the OS knows that
traffic arriving on port 8080 belongs to `sockfd`.

| Parameter   | Value            | Meaning |
|-------------|------------------|---------|
| `sockfd`    | `sockfd`         | The socket to bind. |
| `addr`      | `res->ai_addr`   | Pointer to the address struct (sockaddr_in or sockaddr_in6). |
| `addrlen`   | `res->ai_addrlen`| Byte length of that address struct. |
| return      | 0 on success     | -1 on error. Common error: `EADDRINUSE` (port already in use). |

---

```cpp
if((rv = listen(sockfd, BACKLOG)) == -1){ ... }
```

Marks the socket as **passive** — it will accept incoming connections rather than initiate them.
Before `listen()`, the socket is just an unconnected endpoint. After it, the kernel starts
queuing incoming TCP handshakes.

| Parameter  | Value     | Meaning |
|------------|-----------|---------|
| `sockfd`   | `sockfd`  | The socket to put into listening mode. |
| `backlog`  | `BACKLOG` | Max length of the pending-connection queue. |
| return     | 0 success | -1 on error. |

---

```cpp
addr_size = sizeof(their_addr);
while(true){
```

`addr_size` must be set before passing to `accept()`. The kernel writes the actual address size
back into it, so it's both input (space available) and output (bytes written).

The infinite loop means the server handles one client at a time, forever. After each client
closes, it goes back to `accept()` and waits for the next one.

```cpp
    char buffer[1024];
    clientfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
```

`accept()` **blocks** here until a client connects. When one does, it:
1. Completes the TCP three-way handshake.
2. Fills `their_addr` with the client's IP and port.
3. Returns a brand-new file descriptor (`clientfd`) for this specific connection.

`sockfd` is untouched and keeps listening for future clients.

| Parameter   | Value                              | Meaning |
|-------------|------------------------------------|---------|
| `sockfd`    | `sockfd`                           | The listening socket. |
| `addr`      | `(struct sockaddr *)&their_addr`   | Output: client's address. Cast from `sockaddr_storage*` to `sockaddr*`. |
| `addrlen`   | `&addr_size`                       | In/out: space available → bytes written. |
| return      | new file descriptor                | -1 on error. |

```cpp
    int n = recv(clientfd, buffer, sizeof(buffer), 0);
```

Reads bytes from the client into `buffer`. Blocks until data arrives.

| Parameter | Value             | Meaning |
|-----------|-------------------|---------|
| `sockfd`  | `clientfd`        | The connection-specific socket. |
| `buf`     | `buffer`          | Destination buffer. |
| `len`     | `sizeof(buffer)`  | Max bytes to read (1024). |
| `flags`   | `0`               | No special behavior. Common flag: `MSG_WAITALL` (wait for full length). |
| return    | bytes received    | 0 = connection closed by peer. -1 = error. |

```cpp
    send(clientfd, buffer, n, 0);
```

Sends exactly `n` bytes (what we received) back to the client.

| Parameter | Value      | Meaning |
|-----------|------------|---------|
| `sockfd`  | `clientfd` | The connection-specific socket. |
| `buf`     | `buffer`   | Data to send. |
| `len`     | `n`        | Number of bytes to send. |
| `flags`   | `0`        | No special behavior. Common flag: `MSG_NOSIGNAL` (don't raise SIGPIPE on broken pipe). |
| return    | bytes sent | May be less than `n`; a production server would loop. -1 = error. |

```cpp
    close(clientfd);
}
```

Closes only the **client** connection. The server's `sockfd` is still open and listening.
The client receives a TCP FIN, telling it the server is done sending.

```cpp
freeaddrinfo(res);
```

Frees the linked list that `getaddrinfo` allocated. Unreachable here (infinite loop above),
but the correct cleanup pattern. Always call this after you're done with `res`.

---

## `client.cpp` — Line by Line

The client follows the same structure as the server but uses `connect` instead of
`bind`/`listen`/`accept`, and looks up the server's address rather than its own.

```cpp
#define PORT "8080"
```
Must match the port the server is listening on.

---

```cpp
memset(&hints, 0, sizeof(hints));
hints.ai_family   = AF_UNSPEC;
hints.ai_socktype = SOCK_STREAM;
```

Same as server — zero hints, accept IPv4 or IPv6, want TCP. Note: **no `AI_PASSIVE`** flag.
We're looking up a remote host, not preparing to bind locally.

---

```cpp
if((rv = getaddrinfo("localhost", PORT, &hints, &res)) != 0){ ... }
```

| Parameter | Value         | Meaning |
|-----------|---------------|---------|
| `node`    | `"localhost"` | Hostname to resolve. Resolves to 127.0.0.1 (IPv4) or ::1 (IPv6). |
| `service` | `PORT`        | Port string `"8080"`. |
| `hints`   | `&hints`      | Our filter. |
| `res`     | `&res`        | Output linked list. |

---

```cpp
if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1){ ... }
```

Same as server — creates an unconnected socket endpoint. Parameters identical.

---

```cpp
if((rv = connect(sockfd, res->ai_addr, res->ai_addrlen)) == -1){ ... }
```

Initiates the TCP three-way handshake with the server. Blocks until the connection is
established (or fails). After this returns 0, the socket is fully connected and ready for I/O.

| Parameter  | Value             | Meaning |
|------------|-------------------|---------|
| `sockfd`   | `sockfd`          | Our local socket. |
| `addr`     | `res->ai_addr`    | Server's address (from getaddrinfo). |
| `addrlen`  | `res->ai_addrlen` | Length of that address. |
| return     | 0 on success      | -1 on error. `ECONNREFUSED` = server not running. |

---

```cpp
std::string send_message{readInput()};
send(sockfd, send_message.c_str(), send_message.size(), 0);
```

`send_message.c_str()` — raw `const char*` pointer to the string's data.
`send_message.size()` — byte count, **not** including the null terminator. We send raw data,
not a C string, so the null is intentionally excluded.

```cpp
char buffer[1024];
memset(buffer, 0, sizeof(buffer));
int n = recv(sockfd, buffer, sizeof(buffer), 0);
buffer[n] = '\0';
```

`memset` pre-zeroes the buffer (defensive — not strictly required here since we null-terminate
manually after). After `recv()` returns `n` bytes, we write `'\0'` at position `n` to turn the
raw bytes into a printable C string. `recv` itself never null-terminates.

```cpp
std::cout << "The message sent to the server was : " << send_message << "\n";
std::cout << "The message recevied from the server was : " << buffer << "\n";
```

Prints both sides for comparison.

```cpp
close(sockfd);
freeaddrinfo(res);
```

`close(sockfd)` sends a TCP FIN to the server, signaling end-of-transmission. Then we free
the `addrinfo` linked list. Always call `freeaddrinfo` after you're done with `res`.

---

## Function Signatures

The actual C declarations for every function used in this project.

```c
// resolve a hostname/port into a linked list of addrinfo structs
int getaddrinfo(
    const char           *node,    // hostname ("localhost") or NULL for own address
    const char           *service, // port as string ("8080") or service name ("http")
    const struct addrinfo *hints,  // input: filter describing what you want
    struct addrinfo      **res     // output: pointer to result linked list (you must free this)
);

// create a socket, returns a file descriptor
int socket(
    int domain,    // address family: AF_INET, AF_INET6, AF_UNSPEC
    int type,      // socket type: SOCK_STREAM (TCP), SOCK_DGRAM (UDP)
    int protocol   // protocol: 0 (auto), IPPROTO_TCP, IPPROTO_UDP
);

// assign a local address+port to a socket (server only)
int bind(
    int                    sockfd,  // the socket file descriptor
    const struct sockaddr *addr,    // address to bind to (cast from sockaddr_in/in6)
    socklen_t              addrlen  // byte size of addr
);

// mark socket as passive, start queuing incoming connections (server only)
int listen(
    int sockfd,  // the socket to listen on
    int backlog  // max length of the pending-connection queue
);

// block until a client connects, return a new fd for that connection (server only)
int accept(
    int               sockfd,   // the listening socket
    struct sockaddr  *addr,     // output: filled with the client's address
    socklen_t        *addrlen   // in/out: space available → actual bytes written
);

// initiate a TCP connection to a server (client only)
int connect(
    int                    sockfd,   // the local socket
    const struct sockaddr *addr,     // server's address (from getaddrinfo)
    socklen_t              addrlen   // byte size of addr
);

// send bytes over a connected socket
ssize_t send(
    int         sockfd,  // the socket to send on
    const void *buf,     // pointer to the data to send
    size_t      len,     // number of bytes to send
    int         flags    // 0 for default; MSG_NOSIGNAL to suppress SIGPIPE
);

// receive bytes from a connected socket, blocks until data arrives
ssize_t recv(
    int    sockfd,  // the socket to receive from
    void  *buf,     // destination buffer
    size_t len,     // max bytes to read
    int    flags    // 0 for default; MSG_WAITALL to wait for full len
);

// close a socket, sends TCP FIN to the remote end
int close(int fd);

// free the linked list allocated by getaddrinfo
void freeaddrinfo(struct addrinfo *res);
```

---

## Quick Syscall Reference

| Syscall | Who calls it | Blocking? | Key return |
|---------|-------------|-----------|------------|
| `getaddrinfo` | both | no | 0 = success |
| `socket` | both | no | fd ≥ 0 |
| `bind` | server | no | 0 = success |
| `listen` | server | no | 0 = success |
| `accept` | server | **yes** — waits for client | new fd ≥ 0 |
| `connect` | client | **yes** — waits for handshake | 0 = success |
| `send` | both | usually no | bytes sent |
| `recv` | both | **yes** — waits for data | bytes read, 0 = closed |
| `close` | both | no | 0 = success |
| `freeaddrinfo` | both | no | void |

## Build

```bash
g++ server.cpp -o bin/server
g++ client.cpp -o bin/client
```

Run server first, then client in a separate terminal:

```bash
./bin/server
./bin/client
```
