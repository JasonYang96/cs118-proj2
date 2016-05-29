#include "packet.h"
#include <cstring> // for memset
#include <iostream> // for cout
#include <stdio.h> // for perror
#include <string> // for string
#include <sys/socket.h> // for socket
#include <sys/types.h> // for types
#include <netdb.h> // for getaddrinfo
#include <sys/stat.h> // for open
#include <fcntl.h> // for open
#include <unistd.h> // for close, read
#include <unordered_map> // for map
#include <sys/time.h> // for socket timeout
#include <cmath> // for floor
#include <errno.h>

using namespace std;

void process_error(int status, const string &function);
int open_file(char* file);
int set_up_socket(char* port);
uint16_t update_window(const Packet &p, unordered_map<uint16_t, Packet_info> &window, uint16_t &base_num);
bool valid_ack(const Packet &p, uint16_t base_num);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " PORT-NUMBER FILE-NAME" << endl;
        exit(1);
    }

    int file_fd = open_file(argv[2]);
    int sockfd = set_up_socket(argv[1]);

    struct sockaddr_storage recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    int status, n_bytes;
    uint16_t ack_num, base_num;
    Packet p;

    // select random seq_num
    srand(time(NULL));
    uint16_t seq_num = rand() % MSN;

    // recv SYN from client
    do
    {
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        process_error(n_bytes, "recv SYN");
    } while (!p.syn_set());
    cout << "Debug: Receiving syn packet with seq " << p.seq_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    // sending SYN ACK
    p = Packet(1, 1, 0, seq_num, ack_num, 0, 0, "");
    status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
    process_error(status, "sending SYN ACK");
    cout << "Debug: Sending SYN ACK with seq " << seq_num << endl;
    seq_num = (seq_num + 1) % MSN;
    base_num = seq_num;

    // recv ACK
    do
    {
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        process_error(n_bytes, "recv ACK after SYN ACK");
    } while (!valid_ack(p, base_num)); // discard invalid ack
    cout << "Receiving ACK packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    unordered_map<uint16_t, Packet_info> window;
    double cwnd = min(MSS - 1, MSN / 2);
    uint16_t cwnd_used = 0;
    uint16_t ssthresh = INITIAL_SSTHRESH;
    uint16_t cwd_pkts = 0;
    uint16_t pkts_sent = 0;
    uint16_t dup_ack = 0;
    uint16_t prev_ack = seq_num;
    uint16_t recv_window = UINT16_MAX;
    bool slow_start = true;
    bool congestion_avoidance = false;
    bool fast_recovery = false;
    bool retransmission = false;
    // send file
    do
    {
        if (retransmission) // retransmit missing segment
        {
            // Jacob: take care of retransmission
        }
        else // transmit new segment(s), as allowed
        {
            while (floor(cwnd) - cwnd_used >= 19 && n_bytes != 0)
            {
                string data;
                size_t buf_pos = 0;
                data.resize(MSS - 1);

                cout << "Sending data packet " << seq_num << " " << cwnd << " " << ssthresh << endl;
                // make sure recv all that we can
                do
                {
                    size_t n_to_send = (size_t) min(cwnd - cwnd_used, min(MSS - 1.0, (double) recv_window));
                    n_bytes = read(file_fd, &data[buf_pos], n_to_send);
                    buf_pos += n_bytes;
                    cwnd_used += n_bytes;
                } while (cwnd_used < floor(cwnd) && n_bytes != 0 && buf_pos != MSS - 1);

                // send packet
                p = Packet(0, 0, 0, seq_num, ack_num, buf_pos, 0, data.c_str());
                Packet_info pkt_info = Packet_info(p);
                status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
                process_error(status, "sending packet");
                window.emplace(seq_num, pkt_info);
                seq_num = (seq_num + p.data_len()) % MSN;
                cout << "Debug: sending packet of size " << sizeof(p) + 1 << " with size " << p.data_len() << " and " << p.data().size() << endl;
            }
        }

        // set up timevals
        unordered_map<uint16_t, Packet_info>::const_iterator first_pkt = window.find(base_num);
        if (first_pkt == window.end())
        {
            cerr << "could not find base_num packet in window" << endl;
            exit(1);
        }
        struct timeval base_time = first_pkt->second.get_time_sent();
        
        // create timeout timeval
        struct timeval timeout;
        timeout.tv_usec = INITIAL_TIMEOUT * 1000; // microseconds

        // find max_time for first packet
        struct timeval max_time;
        timeradd(&base_time, &timeout, &max_time);

        // recv ACK
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);

        struct timeval time_left;
        timersub(&max_time, &curr_time, &time_left);

        status = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_left, sizeof(time_left));
        process_error(status, "setsockopt");

        do
        {
            int ack_n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
            if (ack_n_bytes == -1) //error
            {
                // check if timed out
                if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
                {
                    // adjust cwnd and ssthresh
                    ssthresh = cwnd / 2;
                    cwnd = MSS - 1;
                    dup_ack = 0;
                    slow_start = true;
                    congestion_avoidance = false;
                    fast_recovery = false;
                    retransmission = true;
                    break;
                }
                else // else another error and process it
                {
                    process_error(ack_n_bytes, "recv ACK after sending data");
                }
            }
        } while (!valid_ack(p, base_num));
        if (retransmission)
            continue;

        if (prev_ack == p.ack_num()) // if duplicate
        {
            if (fast_recovery)
            {
                cwnd += MSS - 1;
            }
            else
            {
                dup_ack++;
            }

            // if retransmit
            if (dup_ack == 4)
            {
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3 * (MSS - 1);
                fast_recovery = true;
                slow_start = false;
                congestion_avoidance = false;
                dup_ack = 0;
            }

            cout << "Receiving ACK packet " << p.ack_num() << endl;
        }
        else // new ack
        {
            if (slow_start)
            {
                cwnd += MSS - 1;
                dup_ack = 0;

                if (cwnd >= ssthresh)
                {
                    slow_start = false;
                    congestion_avoidance = true;
                    cwd_pkts = cwnd / MSS;
                    pkts_sent = 0;
                }
            }
            else if (congestion_avoidance)
            {
                if (pkts_sent == cwd_pkts)
                {
                    cwd_pkts = cwnd / MSS;
                    pkts_sent = 0;
                }
                cwnd += (MSS - 1) / (double) cwd_pkts;
                pkts_sent++;
            }
            else // fast recovery
            {
                cwnd = ssthresh;
                dup_ack = 0;
                fast_recovery = false;
                congestion_avoidance = true;
            }

            prev_ack = p.ack_num();
            cwnd_used -= update_window(p, window, base_num);
            ack_num = (p.seq_num() + 1) % MSN;
            cout << "Receiving ACK packet " << p.ack_num() << endl;
        }

        // make sure cwnd is not greater than MSN/2
        cwnd = min(cwnd, MSN / 2.0);

        recv_window = p.recv_window();

    } while (n_bytes != 0);

    // TODO: keep recv until window size is not 0

    // send FIN
    p = Packet(0, 0, 1, seq_num, ack_num, 0, 0, "");
    status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
    process_error(status, "sending FIN");
    cout << "Sending data packet " << seq_num << endl;
    seq_num = (seq_num + 1) % MSN;

    // recv FIN ACK
    do
    {
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        process_error(n_bytes, "recv FIN ACK");
        cout << "Receiving ACK packet " << p.ack_num() << endl;
        ack_num = (p.seq_num() + 1) % MSN;
    } while (!p.fin_set() || !p.ack_set());

    // send ACK after FIN ACK
    p = Packet(0, 1, 0, seq_num, ack_num, 0, 0, "");
    status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
    process_error(status, "sending ACK after FIN ACK");
    cout << "Sending data packet " << seq_num << endl;
    seq_num = (seq_num + 1) % MSN;
}

bool valid_ack(const Packet &p, uint16_t base_num)
{
    uint16_t ack = p.ack_num();
    uint16_t max = (base_num + MSN/2) % MSN;

    if (base_num < max) // no overflow of window
    {
        if (ack >= base_num && ack <= max)
        {

            return true;
        }
        else
        {
            cout << "ack_num is " << ack << " and base_num is " << base_num << " and max is " << max << endl;
            return false;
        }
    }
    else // window overflowed
    {
        if (ack > max && ack < base_num)
        {
            cout << "ack_num is " << ack << " and base_num is " << base_num << " and max is " << max << endl;
            return false;
        }
        else
        {
            return true;
        }
    }
}

uint16_t update_window(const Packet &p, unordered_map<uint16_t, Packet_info> &window, uint16_t &base_num)
{
    uint16_t n_removed = 0;

    auto found = window.find(base_num);
    if (found == window.end())
    {
        cerr << "could not find base_num packet in window" << endl;
        exit(1);
    }

    while (base_num < p.ack_num() || ((base_num + found->second.pkt().data_len()) % MSN) == p.ack_num())
    {
        found = window.find(base_num);
        if (found == window.end())
        {
            cerr << "could not find base_num packet in window" << endl;
            exit(1);
        }
        uint16_t len = found->second.pkt().data_len();
        window.erase(base_num);
        n_removed += len;
        base_num = (base_num + len) % MSN;
    }

    return n_removed;
}

int open_file(char* file)
{
    // tries to open file served
    int file_fd = open(file, O_RDONLY);
    if (file_fd < 0)
    {
        cerr << "Could not open " << file << endl;
        exit(1);
    }

    // makes sure it's a regular file
    struct stat buf;
    int status = fstat(file_fd, &buf);
    if (status == -1)
    {
        perror("fstat");
        close(file_fd);
        exit(1);
    }

    if (!S_ISREG(buf.st_mode))
    {
        cerr << file << " is not a regular file" << endl;
        exit(1);
    }

    // TODO: FOR DEBUGGING PURPOSES ONLY, GET RID WHEN DONE
    cout << "file is " << buf.st_size << " bytes" << endl;

    return file_fd;
}

int set_up_socket(char* port)
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
    status = getaddrinfo(NULL, port, &hints, &res);
    if (status != 0)
    {
        cerr << "getaddrinfo error: " << gai_strerror(status) << endl;
        exit(1);
    }

    // find socket to bind to
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

        status = bind(sockfd, res->ai_addr, res->ai_addrlen);
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
