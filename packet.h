#ifndef PACKET_H
#define PACKET_H

class Packet {
public:
    Packet(bool seq, bool ack, bool fin, int seq_num, int ack_num, int cont_len)
    {
        seq = m_seq;
        ack = m_ack;
        fin = m_fin;
        m_seq_num = seq_num;
        m_ack_num = ack_num;
        m_cont_len = cont_len;
    }

private:
    bool m_seq;
    bool m_ack;
    bool m_fin;
    int  m_seq_num;
    int  m_ack_num;
    int  m_cont_len;
};

#endif
