#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <bitset>
#include <cstring>
#include <sys/time.h>

const uint16_t MSS = 20; // MAX IS 1032, CAN'T BE 1024 SINCE OUR HEADER IS MORE THAN 8 BYTES
const uint16_t INITIAL_SSTHRESH = 50; // bytes
const uint16_t INITIAL_TIMEOUT = 500; // ms
const uint16_t MSN = 30720; // bytes

class Packet
{
public:
    Packet() = default;
    Packet(bool syn, bool ack, bool fin, uint16_t seq_num, uint16_t ack_num, uint16_t data_len, uint16_t recv_window, const char *data)
    {
        m_flags[3] = 0;
        m_flags[2] = syn;
        m_flags[1] = ack;
        m_flags[0] = fin;
        m_seq_num = seq_num;
        m_ack_num = ack_num;
        m_data_len = data_len;
        m_recv_window = recv_window;
        strcpy(m_data, data);
    }

    bool syn_set() const
    {
        return m_flags[2];
    }

    bool ack_set() const
    {
        return m_flags[1];
    }

    bool fin_set() const
    {
        return m_flags[0];
    }

    uint16_t seq_num() const
    {
        return m_seq_num;
    }

    uint16_t ack_num() const
    {
        return m_ack_num;
    }

    uint16_t data_len() const
    {
        return m_data_len;
    }

    uint16_t recv_window() const
    {
        return m_recv_window;
    }

    std::string data() const
    {
        return m_data;
    }

private:
    std::bitset<4>  m_flags;    // one byte
    uint16_t        m_seq_num;  // 4 bytes
    uint16_t        m_ack_num;  // 4 bytes
    uint16_t        m_data_len; // 4 bytes
    uint16_t        m_recv_window; // 4 bytes
    char            m_data[MSS]; // MSS bytes
};


class Packet_info
{
public:
    Packet_info(Packet p)
    {
        m_p = p;
        gettimeofday(&m_time_sent, NULL);
    }

    Packet pkt() const
    {
        return m_p;
    }

    struct timeval get_time_sent() const
    {
        return m_time_sent;
    }

private:
    Packet m_p;
    struct timeval m_time_sent;

};
#endif
