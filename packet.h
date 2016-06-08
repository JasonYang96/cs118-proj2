#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <bitset>
#include <cstring>
#include <sys/time.h>

const uint16_t MSS = 1024; // MAX IS 1032 but header is 8 bytes
const uint16_t HEADER_LEN = 8; // bytes
const uint16_t INITIAL_SSTHRESH = 15360; // bytes
const uint16_t INITIAL_TIMEOUT = 500; // ms
const uint16_t MSN = 15360; // bytes

class Packet
{
public:
    Packet() = default;
    Packet(bool syn, bool ack, bool fin, uint16_t seq_num, uint16_t ack_num, uint16_t recv_window, const char *data, size_t len)
    {
        m_syn = syn;
        m_ack = ack;
        m_fin = fin;
        m_seq_num = seq_num;
        m_ack_num = ack_num;
        m_recv_window = recv_window;
        strncpy(m_data, data, len);
    }

    bool syn_set() const
    {
        return m_syn;
    }

    bool ack_set() const
    {
        return m_ack;
    }

    bool fin_set() const
    {
        return m_fin;
    }

    uint16_t seq_num() const
    {
        return m_seq_num;
    }

    uint16_t ack_num() const
    {
        return m_ack_num;
    }

    uint16_t recv_window() const
    {
        return m_recv_window;
    }

    void data(char* buffer, size_t len) const
    {
        strncpy(buffer, m_data, len);
    }

private:
    bool     m_syn:1;
    bool     m_ack:1;
    bool     m_fin:1;
    uint16_t m_seq_num;  // 2 bytes
    uint16_t m_ack_num;  // 2 bytes
    uint16_t m_recv_window; // 2 bytes
    char     m_data[MSS]; // MSS bytes
};

class Packet_info
{
public:
    Packet_info() = default;
    Packet_info(Packet p, uint16_t data_len)
    {
        m_p = p;
        m_data_len = data_len;

        gettimeofday(&m_time_sent, NULL);

        // create timeout timeval
        struct timeval timeout;
        timeout.tv_usec = INITIAL_TIMEOUT * 1000; // microseconds

        // find max_time for first packet
        timeradd(&m_time_sent, &timeout, &m_max_time);
    }

    Packet pkt() const
    {
        return m_p;
    }

    uint16_t data_len() const
    {
        return m_data_len;
    }

    struct timeval get_max_time() const
    {
        return m_max_time;
    }

    void update_time()
    {
        gettimeofday(&m_time_sent, NULL);

        // create timeout timeval
        struct timeval timeout;
        timeout.tv_usec = INITIAL_TIMEOUT * 1000; // microseconds

        // find max_time for first packet
        timeradd(&m_time_sent, &timeout, &m_max_time);
    }

private:
    Packet m_p;
    struct timeval m_time_sent;
    struct timeval m_max_time;
    uint16_t       m_data_len;
};


#endif
