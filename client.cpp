#include "packet.h"
#include <cstring> // for memset
#include <iostream> // for cout
#include <stdio.h> // for perror
#include <string> // for string
#include <sys/socket.h> // for socket
#include <sys/types.h> // for types
#include <netdb.h> // for getaddrinfo
#include <ctime> // for time
#include <cstdlib> // for srand, rand
#include <unordered_map>
#include <fstream>

using namespace std;

const uint16_t MAX_RECV_WINDOW = 30720;

void process_error(int status, const string &function);
int set_up_socket(char* argv[]);
bool valid_pkt(const Packet &p, uint16_t base_num);
struct timeval time_left(const Packet_info &last_ack);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " SERVER-HOST-OR-IP PORT-NUMBER" << endl;
        return 1;
    }

    int sockfd = set_up_socket(argv);
    int status, n_bytes;
    Packet p;
    Packet_info last_ack;
    struct timeval time_left_tv;

    // select random seq_num
    srand(time(NULL));
    uint16_t seq_num = rand() % MSN;
    uint16_t base_num;

    // send SYN segment
    p = Packet(1, 0, 0, seq_num, 0, 0, MAX_RECV_WINDOW, "");
    last_ack = Packet_info(p);
    status = send(sockfd, (void *) &p, sizeof(p), 0);
    process_error(status, "sending SYN");
    seq_num = (seq_num + 1) % MSN; // SYN packet takes up 1 sequence
    cout << "Debug: Sending syn packet with seq " << p.seq_num() << endl;

    // recv SYN ACK
    do
    {
        time_left_tv = time_left(last_ack);
        status = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_left_tv, sizeof(time_left_tv));
        process_error(status, "setsockopt");
        n_bytes = recv(sockfd, (void *) &p, sizeof(p), 0);
        process_error(n_bytes, "recv SYN ACK");

        // TODO: Process recv
    } while (!p.syn_set() || !p.ack_set());
    cout << "Debug: Receiving syn ack packet with seq " << p.seq_num() << endl;
    base_num = (p.seq_num() + 1) % MSN;

    // send ACK after SYN ACK
    p = Packet(0, 1, 0, seq_num, base_num, 0, MAX_RECV_WINDOW, "");
    last_ack = Packet_info(p);
    status = send(sockfd, (void *) &p, sizeof(p), 0);
    process_error(status, "sending ACK after SYN ACK");
    cout << "Sending ACK packet " << p.ack_num() << endl;
    seq_num = (seq_num + 1) % MSN;

    // receive until a FIN segment is recv'd
    unordered_map<uint16_t, Packet_info> window;
    ofstream output("file");
    while (1)
    {
        // discard invalid acks
        do
        {
            time_left_tv = time_left(last_ack);
            status = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_left_tv, sizeof(time_left_tv));
            process_error(status, "setsockopt");
            n_bytes = recv(sockfd, (void *) &p, sizeof(p), 0);
            process_error(n_bytes, "recv file");

            // TODO: process recv
        } while (!valid_pkt(p, base_num));

        // update window
        window.emplace(p.seq_num(), Packet_info(p));
        for (auto it = window.find(base_num); it != window.end(); it = window.find(base_num))
        {
            output << it->second.pkt().data();
            base_num = (base_num + it->second.pkt().data_len()) % MSN;
            window.erase(it);
        }

        // send FIN ACK if FIN segment
        if (p.fin_set())
        {
            cout << "Debug: recv FIN packet with seq " << p.seq_num() << endl;
            base_num = (base_num + 1) % MSN; //consumed fin segment
            p = Packet(0, 1, 1, seq_num, base_num, 0, MAX_RECV_WINDOW, "");
            last_ack = Packet_info(p);
            status = send(sockfd, (void *) &p, sizeof(p), 0);
            process_error(status, "sending FIN ACK");
            cout << "Sending ACK packet " << p.ack_num() << endl;
            seq_num = (seq_num + 1) % MSN;
            break;
        }
        else // data segment so send ACK
        {
            cout << "Debug: recv file with size " << p.data_len() << " and " << p.data().size() << endl;
            cout << "Receiving data packet " << p.seq_num() << endl;
            p = Packet(0, 1, 0, seq_num, base_num, 0, MAX_RECV_WINDOW - sizeof(window), "");
            last_ack = Packet_info(p);
            status = send(sockfd, (void *) &p, sizeof(p), 0);
            process_error(status, "sending ACK for data packet");
            cout << "Sending ACK packet " << p.ack_num() << endl;
            seq_num = (seq_num + 1) % MSN;
        }
    }
    output.close();

    // recv ACK
    do
    {

        time_left_tv = time_left(last_ack);
        status = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_left_tv, sizeof(time_left_tv));
        process_error(status, "setsockopt");
        n_bytes = recv(sockfd, (void *) &p, sizeof(p), 0);
        process_error(n_bytes, "recv ACK after FIN ACK");
        cout << "Debug: Receiving ack packet after FIN ACK " << p.seq_num() << endl;

        // TODO: Process recv
    } while (!valid_pkt(p, base_num)); // discard invalid acks
}

struct timeval time_left(const Packet_info &last_ack)
{
    struct timeval max_time = last_ack.get_max_time();

    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);

    struct timeval time_left;
    timersub(&max_time, &curr_time, &time_left);

    return time_left;
}

bool valid_pkt(const Packet &p, uint16_t base_num)
{
    uint16_t seq = p.seq_num();
    uint16_t max = (base_num + MSN/2) % MSN;

    if (base_num < max) // no overflow of window
    {
        if (seq >= base_num && seq <= max)
        {
            return true;
        }
        else
        {
            cout << "seq_num is " << seq << " and base_num is " << base_num << " and max is " << max << endl;
            return false;
        }
    }
    else // window overflowed
    {
        if (seq > max && seq < base_num)
        {
            cout << "seq_num is " << seq << " and base_num is " << base_num << " and max is " << max << endl;
            return false;
        }
        else
        {
            return true;
        }
    }
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
