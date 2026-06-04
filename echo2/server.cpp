#include <iostream>
#include <cstring>
#include <sys/types.h>    
#include <sys/socket.h>   
#include <netdb.h>        
#include <arpa/inet.h>    
#include <netinet/in.h>   
#include <unistd.h>       
#include <cstdint>

#define PORT "8080"
#define BACKLOG 10        // max queued connections

int main() {

    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    int sockfd, clientfd;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints)); 
    hints.ai_family   = AF_UNSPEC;     // can choose any IP, IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;   // choosing sock_stream to get reliable tcp connection
    hints.ai_flags    = AI_PASSIVE;    // fill in my own IP automatically

    int rv;
    if((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0){
        std::cerr << "Error when trying to get own address\n";
        return 0;
    }

    // socket
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(sockfd == -1){
        std::cerr << "Error when trying to get a socket\n";
        return 1;
    }

    // allow reuse of port immediately after server stops
    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // bind
    if((rv = bind(sockfd, res->ai_addr, res->ai_addrlen)) == -1){
        std::cerr << "Error when trying to bind socket to port\n";
        return 1;
    }
    
    // listen
    if((rv = listen(sockfd, BACKLOG)) == -1){
        std::cerr << "Error when trying to listen on socket with file descriptor : " << sockfd << "\n";
        return 1;
    }

    // accept
    // recv + send loop
    // close

    addr_size = sizeof(their_addr);
    while(true){
        char buffer[1024];
        clientfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

        uint16_t msglen;
        recv(clientfd, &msglen, sizeof(msglen), 0);
        msglen = ntohs(msglen);

        int n = recv(clientfd, buffer, msglen, 0);
        send(clientfd, buffer, n, 0);

        close(clientfd);
    }

    freeaddrinfo(res);

    return 0;
}