#include <iostream>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT "8080"

std::string readInput(){
    std::cout << "Please enter the message you want to see get echoed : ";
    std::string inputMessage{};
    std::cin >> inputMessage;
    return inputMessage;
}

int main() {
    int sockfd;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rv;
    if((rv = getaddrinfo("localhost", PORT, &hints, &res)) != 0){
        std::cerr << "Error when trying to get own address\n";
        return 0;
    }

    // socket
    if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1){
        std::cerr << "Error when trying to get a socket\n";
        return 1;
    }

    // connect
    if((rv = connect(sockfd, res->ai_addr, res->ai_addrlen)) == -1){
        std::cerr << "Error when trying to connect to server address\n";
        return 1;
    }

    // send + recv
    std::string send_message{readInput()};
    send(sockfd, send_message.c_str(), send_message.size(), 0);
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    int n = recv(sockfd, buffer, sizeof(buffer), 0);

    buffer[n] = '\0';

    std::cout << "The message sent to the server was : " << send_message << "\n";
    std::cout << "The message recevied from the server was : " << buffer << "\n";

    // close
    close(sockfd);

    freeaddrinfo(res);

    return 0;
}