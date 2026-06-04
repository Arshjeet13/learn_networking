Process : 
Server
We use getaddrinfo() to get the local machine's own IP address. 
res is a pointer to the struct addrinfo, which looks like this : 
struct addrinfo {
    int              ai_flags;      // hints: AI_PASSIVE, AI_CANONNAME, etc.
    int              ai_family;     // AF_INET, AF_INET6, AF_UNSPEC
    int              ai_socktype;   // SOCK_STREAM, SOCK_DGRAM
    int              ai_protocol;   // IPPROTO_TCP, IPPROTO_UDP, 0 for auto
    socklen_t        ai_addrlen;    // length of ai_addr
    struct sockaddr *ai_addr;       // pointer to sockaddr_in or sockaddr_in6
    char            *ai_canonname;  // canonical name of the host
    struct addrinfo *ai_next;       // next node in linked list
};