#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <sstream>
#include <bitset>
#include <cstring>

class Header {
public:
    Header() = default;
    Header(bool syn, bool ack, bool fin, uint16_t seq_num, uint16_t ack_num, uint16_t cont_len)
    {
        m_flags[3] = 0;
        m_flags[2] = syn;
        m_flags[1] = ack;
        m_flags[0] = fin;
        m_seq_num = seq_num;
        m_ack_num = ack_num;
        m_cont_len = cont_len;
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

    uint16_t cont_len()
    {
        return m_cont_len;
    }

private:
    std::bitset<4>   m_flags;
    uint16_t         m_seq_num;
    uint16_t         m_ack_num;
    uint16_t         m_cont_len;
};

class Packet {
public:
    Packet() = default;
    Packet(bool seq, bool ack, bool fin, uint16_t seq_num, uint16_t ack_num, uint16_t cont_len, char *data)
    {
        m_header = Header(seq, ack, fin, seq_num, ack_num, cont_len);
        strcpy(m_data, data);
    }

    bool syn_set()
    {
        return m_header.syn_set();
    }

    bool ack_set()
    {
        return m_header.ack_set();
    }

    bool fin_set()
    {
        return m_header.fin_set();
    }

    uint16_t seq_num()
    {
        return m_header.seq_num();
    }

    uint16_t ack_num()
    {
        return m_header.ack_num();
    }

    uint16_t cont_len()
    {
        return m_header.cont_len();
    }

    std::string data()
    {
        return m_data;
    }

private:
    Header m_header;
    char   m_data[512];
};

#endif
