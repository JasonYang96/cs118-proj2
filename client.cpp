#include "packet.h"
#include <cstring> // for memset
#include <iostream> // for cout
#include <stdio.h> // for perror
#include <string> // for string
#include <sys/socket.h> // for socket
#include <sys/types.h> // for types
#include <netdb.h> // for getaddrinfo

using namespace std;

void process_error(int status, const string &function);
int set_up_socket(char* argv[]);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " SERVER-HOST-OR-IP PORT-NUMBER" << endl;
        return 1;
    }

    int sockfd = set_up_socket(argv);
    Packet p;

    // send SYN segment
    int status;
    p = Packet(1, 0, 0, 12, 0, 0, "test5");
    status = send(sockfd, (void *) &p, sizeof(p), 0);
    process_error(status, "sending SYN");
    cout << "sending SYN" << endl;

    // recv packet
    int n_bytes = recv(sockfd, (void *) &p, sizeof(p), 0);
    process_error(n_bytes, "recv SYN ACK");
    cout << "receiving SYN ACK" << endl;
    
    // ACK if SYN ACK segment
    if (p.syn_set() && p.ack_set())
    {
        p = Packet(0, 1, 0, 13, 0, 0, "dr");
        status = send(sockfd, (void *) &p, sizeof(p), 0);
        process_error(status, "sending ACK after SYN ACK");
        cout << "sending ACK" << endl;
    }

    while(1) {}
}

int set_up_socket(char* argv[])
{
    struct addrinfo hints;
    struct addrinfo *res;
    int status;

    // set up hints addrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    //set up socket calls
    status = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (status != 0)
    {
        cerr << "getaddrinfo error: " << gai_strerror(status) << endl;
        exit(1);
    }

    // find socket to connect to
    int sockfd;
    int yes = 1;
    auto i = res;
    for (; i != NULL; i = i ->ai_next)
    {
        sockfd = socket(res->ai_family, res->ai_socktype, 0);
        if (sockfd == -1)
        {
            continue;
        }

        status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (status == -1)
        {
            continue;
        }

        status = connect(sockfd, res->ai_addr, res->ai_addrlen);
        if (status == -1)
        {
            continue;
        }

        break;
    }
    freeaddrinfo(res);

    // check if reached end of linked list
    // means could not find a socket to bind to
    if (i == NULL)
    {
        perror("bind to a socket");
        exit(1);
    }

    return sockfd;
}

void process_error(int status, const string &function)
{
    if (status == -1)
    {
        perror(&function[0]);
        exit(1);
    }
}
