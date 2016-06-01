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
#include <unistd.h> // for close
#include <unordered_map>
#include <fstream>

using namespace std;

const uint16_t MAX_RECV_WINDOW = 30720;

void process_error(int status, const string &function);
void process_recv(int n_bytes, const string &function, int sockfd, Packet_info &last_ack);
int set_up_socket(char* argv[]);
bool valid_pkt(const Packet &p, uint16_t base_num, const unordered_map<uint16_t, Packet_info> &window);
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
    p = Packet(1, 0, 0, seq_num, 0, MAX_RECV_WINDOW, "", 0);
    last_ack = Packet_info(p, 0);
    status = send(sockfd, (void *) &p, HEADER_LEN, 0);
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
        process_recv(n_bytes, "recv SYN ACK", sockfd, last_ack);
    } while (!p.syn_set() || !p.ack_set());
    cout << "Debug: Receiving syn ack packet with seq " << p.seq_num() << endl;
    base_num = (p.seq_num() + 1) % MSN;

    // send ACK after SYN ACK
    p = Packet(0, 1, 0, seq_num, base_num, MAX_RECV_WINDOW, "", 0);
    last_ack = Packet_info(p, n_bytes - HEADER_LEN);
    status = send(sockfd, (void *) &p, HEADER_LEN, 0);
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
            process_recv(n_bytes, "recv file", sockfd, last_ack);
        } while (!valid_pkt(p, base_num, window));

        // update window
        last_ack = Packet_info(p, n_bytes - HEADER_LEN);
        window.emplace(p.seq_num(), last_ack);
        for (auto it = window.find(base_num); it != window.end(); it = window.find(base_num))
        {
            size_t len = it->second.data_len();
            char buffer[len];
            it->second.pkt().data(buffer, len);
            output << buffer;
            base_num = (base_num + len) % MSN;
            window.erase(it);
        }

        // send FIN ACK if FIN segment
        if (p.fin_set())
        {
            cout << "Debug: recv FIN packet with seq " << p.seq_num() << endl;
            base_num = (p.seq_num() + 1) % MSN; //consumed fin segment
            p = Packet(0, 1, 1, seq_num, base_num, MAX_RECV_WINDOW, "", 0);
            last_ack = Packet_info(p, 0);
            status = send(sockfd, (void *) &p, HEADER_LEN, 0);
            process_error(status, "sending FIN ACK");
            cout << "Sending ACK packet " << p.ack_num() << endl;
            seq_num = (seq_num + 1) % MSN;
            break;
        }
        else // data segment so send ACK
        {
            cout << "Debug: recv file with size " << last_ack.data_len() << endl;
            cout << "Receiving data packet " << p.seq_num() << endl;
            p = Packet(0, 1, 0, seq_num, base_num, MAX_RECV_WINDOW - sizeof(window), "", 0);
            status = send(sockfd, (void *) &p, HEADER_LEN, 0);
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
        process_recv(n_bytes, "recv ACK after FIN ACK", sockfd, last_ack);
    } while (p.seq_num() != base_num); // discard invalid acks
    cout << "Debug: Receiving ack packet after FIN ACK " << p.seq_num() << endl;

    close(sockfd);
}

void process_recv(int n_bytes, const string &function, int sockfd, Packet_info &last_ack)
{
    if (n_bytes == -1) //error
    {
        // check if timed out
        if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
        {
            // retransmit last packet
            last_ack.update_time();
            Packet p = last_ack.pkt();
            int status = send(sockfd, (void *) &p, sizeof(p), 0);
            process_error(status, "sending FIN ACK");
            cout << "Sending ACK packet " << p.ack_num() << " Retransmission" << endl;
        }
        else // else another error and process it
        {
            process_error(n_bytes, function);
        }
    }
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

bool valid_pkt(const Packet &p, uint16_t base_num, const unordered_map<uint16_t, Packet_info> &window)
{
    uint16_t seq = p.seq_num();
    uint16_t max = (base_num + MSN/2) % MSN;

    if (base_num < max) // no overflow of window
    {
        if (seq >= base_num && seq <= max && window.find(seq) == window.end())
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else // window overflowed
    {
        if (seq > max && seq < base_num && window.find(seq) == window.end())
        {
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
