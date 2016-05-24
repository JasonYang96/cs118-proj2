#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <bitset>
#include <cstring>
#include <ctime>
#include <sys/time.h>

const size_t MSS = 20; // MAX IS 1032, CAN'T BE 1024 SINCE OUR HEADER IS MORE THAN 8 BYTES
const size_t INITIAL_SSTHRESH = 1000;
const size_t INITIAL_TIMEOUT = 500; // ms
const uint16_t MSN = 30720;

class Packet
{
public:
    Packet() = default;
    Packet(bool syn, bool ack, bool fin, uint16_t seq_num, uint16_t ack_num, uint16_t data_len, const char *data)
    {
        m_flags[3] = 0;
        m_flags[2] = syn;
        m_flags[1] = ack;
        m_flags[0] = fin;
        m_seq_num = seq_num;
        m_ack_num = ack_num;
        m_data_len = data_len;
        strcpy(m_data, data);
    }

    bool syn_set()
    {
        return m_flags[2];
    }

    bool ack_set()
    {
        return m_flags[1];
    }

    bool fin_set()
    {
        return m_flags[0];
    }

    uint16_t seq_num()
    {
        return m_seq_num;
    }

    uint16_t ack_num()
    {
        return m_ack_num;
    }

    uint16_t data_len()
    {
        return m_data_len;
    }

    std::string data()
    {
        return m_data;
    }

private:
    std::bitset<4>   m_flags;    // one byte
    uint16_t         m_seq_num;  // 4 bytes
    uint16_t         m_ack_num;  // 4 bytes
    uint16_t         m_data_len; // 4 bytes
    char             m_data[MSS];
};


class Packet_info
{
public:
    Packet_info(Packet p)
    {
        m_p = p;
        gettimeofday(&m_time_sent, NULL);
    }

    Packet pkt()
    {
        return m_p;
    }

    struct timeval get_time_sent()
    {
        return m_time_sent;
    }

private:
    Packet m_p;
    struct timeval m_time_sent;

};
#endif
