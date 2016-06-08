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
#include <signal.h> // for SIG_INT detection
#include <fstream> // for ifstream
#include <errno.h>

using namespace std;

uint16_t update_window(const Packet &p, unordered_map<uint16_t, Packet_info> &window, uint16_t &base_num, RTO &rto);
struct timeval time_left(unordered_map<uint16_t, Packet_info> &window, uint16_t base_num);
void process_recv(int n_bytes, const string &function, int sockfd, Packet_info &last_ack, struct sockaddr_storage recv_addr, socklen_t addr_len, const RTO &rto);
void process_error(int status, const string &function);
bool valid_ack(const Packet &p, uint16_t base_num);
int open_file(char* file);
int set_up_socket(char* port);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " PORT-NUMBER FILE-NAME" << endl;
        exit(1);
    }

    ifstream file(argv[2]);
    int sockfd = set_up_socket(argv[1]);
    struct sockaddr_storage recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    int status, n_bytes;
    uint16_t ack_num, base_num;
    Packet p;

    unordered_map<uint16_t, Packet_info> window;
    Packet_info pkt_info;

    // select random seq_num
    srand(time(NULL));
    uint16_t seq_num = rand() % MSN;

    RTO rto;

    // recv SYN from client
    do
    {
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        process_error(n_bytes, "recv SYN");
    } while (!p.syn_set());
    cout << "Receiving packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    // sending SYN ACK
    p = Packet(1, 1, 0, seq_num, ack_num, 0, "", 0);
    pkt_info = Packet_info(p, n_bytes - HEADER_LEN, rto.get_timeout());
    status = sendto(sockfd, (void *) &p, HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
    process_error(status, "sending SYN ACK");
    cout << "Sending packet " << seq_num << " " << MSS << " " << INITIAL_SSTHRESH << " SYN" << endl;
    seq_num = (seq_num + 1) % MSN;
    base_num = seq_num;

    // recv ACK
    do
    {
        struct timeval max_time = pkt_info.get_max_time();
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);
        struct timeval time_left;
        timersub(&max_time, &curr_time, &time_left);
        status = setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&time_left, sizeof(time_left));
        process_error(status, "setsockopt");
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        process_recv(n_bytes, "recv ACK after SYN ACK", sockfd, pkt_info, recv_addr, addr_len, rto);
    } while (p.seq_num() != ack_num); // discard invalid ack
    uint16_t prev_ack = p.ack_num();
    cout << "Receiving packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    double cwnd = min((double) MSS, MSN / 2.0);
    uint16_t cwnd_used = 0;
    uint16_t ssthresh = INITIAL_SSTHRESH;
    uint16_t cwd_pkts = 0;
    uint16_t pkts_sent = 0;
    uint16_t dup_ack = 0;
    uint16_t recv_window = UINT16_MAX;
    bool slow_start = true;
    bool congestion_avoidance = false;
    bool fast_recovery = false;
    bool retransmission = false;
    bool last_retransmit = false;
    // send file
    do
    {
        if (retransmission) // retransmit missing segment
        {
            auto found = window.find(base_num);
            p = found->second.pkt();
            status = sendto(sockfd, (void *) &p, found->second.data_len() + HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
            process_error(status, "sending retransmission");
            cout << "Sending packet " << p.seq_num() << " " << cwnd << " " << ssthresh << " Retransmission" << endl;
            retransmission = false;

            if (last_retransmit)
            {
                // double RTO if retransmission again
                rto.double_RTO();
            }
            found->second.update_time(rto.get_timeout());
            last_retransmit = true;
        }
        else // transmit new segment(s), as allowed
        {
            last_retransmit = false;
            while (floor(cwnd) - cwnd_used >= MSS && !file.eof())
            {
                string data;
                size_t buf_pos = 0;
                data.resize(MSS);

                cout << "Sending packet " << seq_num << " " << cwnd << " " << ssthresh << endl;
                // make sure recv all that we can
                do
                {
                    size_t n_to_send = (size_t) min(cwnd - cwnd_used, (double) min(MSS, recv_window));
                    file.read(&data[buf_pos], n_to_send);
                    n_bytes = file.gcount();
                    buf_pos += n_bytes;
                    cwnd_used += n_bytes;
                } while (cwnd_used < floor(cwnd) && !file.eof() && buf_pos != MSS);

                // send packet
                p = Packet(0, 0, 0, seq_num, ack_num, 0, data.c_str(), buf_pos);
                pkt_info = Packet_info(p, buf_pos, rto.get_timeout());
                status = sendto(sockfd, (void *) &p, buf_pos + HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
                process_error(status, "sending packet");
                window.emplace(seq_num, pkt_info);
                seq_num = (seq_num + pkt_info.data_len()) % MSN;
            }
        }

        // recv ACK
        do
        {
            struct timeval time_left_tv= time_left(window, base_num);
            status = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_left_tv, sizeof(time_left_tv));
            process_error(status, "setsockopt");
            int ack_n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
            if (ack_n_bytes == -1) //error
            {
                // check if timed out
                if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
                {
                    // adjust cwnd and ssthresh
                    ssthresh = cwnd / 2;
                    cwnd = MSS;
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

        cout << "Receiving packet " << p.ack_num() << endl;
        if (prev_ack == p.ack_num()) // if duplicate
        {
            if (fast_recovery)
            {
                cwnd += MSS;
            }
            else
            {
                dup_ack++;
            }

            // if retransmit
            if (dup_ack == 3)
            {
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3 * MSS;
                fast_recovery = true;
                slow_start = false;
                congestion_avoidance = false;
                retransmission = true;
                dup_ack = 0;
            }
        }
        else // new ack
        {
            if (slow_start)
            {
                cwnd += MSS;
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
                cwnd += MSS / (double) cwd_pkts;
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
            cwnd_used -= update_window(p, window, base_num, rto);
            ack_num = (p.seq_num() + 1) % MSN;
        }

        cwnd = min(cwnd, MSN / 2.0); // make sure cwnd is not greater than MSN/2
        cwnd = max(cwnd, (double) MSS); // make sure cwnd is not less than MSS
        ssthresh = max(ssthresh, MSS); // make sure ssthresh is at least MSS

        recv_window = p.recv_window();
    } while (!file.eof() || (window.size() != 0));

    // send FIN
    p = Packet(0, 0, 1, seq_num, ack_num, 0, "", 0);
    pkt_info = Packet_info(p, 0, rto.get_timeout());
    status = sendto(sockfd, (void *) &p, HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
    process_error(status, "sending FIN");
    cout << "Sending packet " << seq_num << " FIN" << endl;
    seq_num = (seq_num + 1) % MSN;

    // recv FIN ACK
    do
    {
        struct timeval max_time = pkt_info.get_max_time();
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);
        struct timeval time_left;
        timersub(&max_time, &curr_time, &time_left);
        status = setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&time_left, sizeof(time_left));
        process_error(status, "setsockopt");
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        process_recv(n_bytes, "recv FIN ACK", sockfd, pkt_info, recv_addr, addr_len, rto);
    } while (!p.fin_set() || !p.ack_set());
    prev_ack = p.ack_num();
    cout << "Receiving packet " << p.ack_num() << endl;
    ack_num = (p.seq_num() + 1) % MSN;

    // send ACK after FIN ACK
    p = Packet(0, 1, 0, seq_num, ack_num, 0, "", 0);
    pkt_info = Packet_info(p, 0, rto.get_timeout());
    status = sendto(sockfd, (void *) &p, HEADER_LEN, 0, (struct sockaddr *) &recv_addr, addr_len);
    process_error(status, "sending ACK after FIN ACK");
    cout << "Sending packet " << seq_num << endl;

    // make sure client receives ACK after FIN ACK
    do
    {
        struct timeval max_time = pkt_info.get_max_time();
        struct timeval curr_time;
        gettimeofday(&curr_time, NULL);
        struct timeval time_left;
        timersub(&max_time, &curr_time, &time_left);
        time_left.tv_sec *= 2; // 2*RTO
        time_left.tv_usec *= 2; // 2*RTO
        status = setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&time_left, sizeof(time_left));
        process_error(status, "setsockopt");
        n_bytes = recvfrom(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, &addr_len);
        if (n_bytes == -1) //error
        {
            // check if timed out, if so everything is fine close
            if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
            {
                break;
            }
            else // else another error and process it
            {
                process_error(n_bytes, "checking to see if recv FIN ACK again");
            }
        }
        else if (p.fin_set() && p.ack_set()) // if client send FIN ACK again, send ACK
        {
            pkt_info.update_time(rto.get_timeout());
            p = pkt_info.pkt();
            status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
            process_error(status, "sending packet");
        }
    } while (!p.fin_set() || !p.ack_set());
}

void process_recv(int n_bytes, const string &function, int sockfd, Packet_info &last_ack, struct sockaddr_storage recv_addr, socklen_t addr_len, const RTO &rto)
{
    if (n_bytes == -1) //error
    {
        // check if timed out
        if (errno == EAGAIN || EWOULDBLOCK || EINPROGRESS)
        {
            last_ack.update_time(rto.get_timeout());
            Packet p = last_ack.pkt();
            int status = sendto(sockfd, (void *) &p, sizeof(p), 0, (struct sockaddr *) &recv_addr, addr_len);
            process_error(status, "sending packet");
        }
        else // else another error and process it
        {
            process_error(n_bytes, "checking to see if recv FIN ACK again");
        }
    }
}

struct timeval time_left(unordered_map<uint16_t, Packet_info> &window, uint16_t base_num)
{
    auto first_pkt = window.find(base_num);
    if (first_pkt == window.end())
    {
        cerr << "could not find base_num packet " << base_num << " in window, in time_left function" << endl;
        exit(1);
    }
    struct timeval max_time = first_pkt->second.get_max_time();

    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);

    struct timeval time_left;
    timersub(&max_time, &curr_time, &time_left);

    return time_left;
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
            return false;
        }
    }
    else // window overflowed
    {
        if (ack > max && ack < base_num)
        {
            return false;
        }
        else
        {
            return true;
        }
    }
}

uint16_t update_window(const Packet &p, unordered_map<uint16_t, Packet_info> &window, uint16_t &base_num, RTO &rto)
{
    uint16_t n_removed = 0;

    while (base_num > p.ack_num())
    {
        auto found = window.find(base_num);
        if (found == window.end())
        {
            cerr << "could not find base_num packet with base_num " << base_num << " and ack num " << p.ack_num() << " in window, update_window" << endl;
            exit(1);
        }
        rto.update_RTO(found->second.get_time_sent());
        uint16_t len = found->second.data_len();
        window.erase(base_num);
        n_removed += len;
        base_num = (base_num + len) % MSN;
    }
    while (base_num < p.ack_num())
    {
        auto found = window.find(base_num);
        if (found == window.end())
        {
            cerr << "could not find base_num packet with base_num " << base_num << " and ack num " << p.ack_num() << " in window, update_window" << endl;
            exit(1);
        }
        rto.update_RTO(found->second.get_time_sent());
        uint16_t len = found->second.data_len();
        window.erase(base_num);
        n_removed += len;
        base_num = (base_num + len) % MSN;
    }

    return n_removed;
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
