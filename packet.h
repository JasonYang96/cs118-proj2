#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <sstream>

class Header {
public:
    Header() {};
    Header(char seq, char ack, char fin, const std::string &seq_num, const std::string &ack_num, const std::string &cont_len)
    {
        seq = m_syn;
        ack = m_ack;
        fin = m_fin;
        m_seq_num = seq_num;
        m_ack_num = ack_num;
        m_cont_len = cont_len;
    }

    void encode(std::stringstream &ss)
    {
        ss << m_syn << m_ack << m_fin << m_seq_num << m_ack_num << m_cont_len;
    }

private:
    char        m_syn;
    char        m_ack;
    char        m_fin;
    std::string m_seq_num;
    std::string m_ack_num;
    std::string m_cont_len;
};

class Packet {
public:
    Packet(char seq, char ack, char fin, const std::string &seq_num, const std::string &ack_num, const std::string &cont_len, const std::string &data)
    {
        m_header = Header(seq, ack, fin, seq_num, ack_num, cont_len);
        size_t len = size_t(stoll(cont_len));
        m_data.resize(len);
        m_data = std::string(data, 0, len);
    }

    std::string encode()
    {
        std::stringstream ss;
        m_header.encode(ss);
        ss << m_data;
        return ss.str();
    }

private:
    Header      m_header;
    std::string m_data;
};

#endif
