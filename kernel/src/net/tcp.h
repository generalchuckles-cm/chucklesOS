#ifndef TCP_H
#define TCP_H

#include <cstdint>
#include "defs.h"

// TCP Flags
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

struct TCPHeader {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;      // Big Endian
    uint32_t ack_num;      // Big Endian
    uint8_t  data_offset;  // 4 bits reserved, 4 bits header length (in 32-bit words)
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed));

enum TcpState {
    CLOSED,
    SYN_SENT,
    ESTABLISHED,
    FIN_WAIT
};

class TcpSocket {
public:
    TcpSocket();
    
    // Connect to a remote IP/Port (Blocking 3-way handshake)
    bool connect(uint32_t dest_ip, uint16_t dest_port);
    
    // Send data (Blocking PSH+ACK)
    bool send(const uint8_t* data, uint32_t len);
    
    // Receive data (Blocking)
    // Returns bytes read, or -1 on error/close
    int recv(uint8_t* buffer, uint32_t max_len);
    
    // Close connection (Send FIN)
    void close();
    
    // Called by NetworkStack when a TCP packet arrives for this port
    void handle_packet(TCPHeader* header, uint8_t* data, uint16_t len);

    bool is_connected() { return state == ESTABLISHED; }
    uint16_t get_local_port() { return local_port; }

private:
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    
    uint32_t seq_num;
    uint32_t ack_num;
    
    volatile TcpState state;
    
    // Receive Buffer (Simple circular or linear buffer)
    static const int RX_BUF_SIZE = 8192;
    uint8_t* rx_buffer;
    volatile int rx_head;
    volatile int rx_tail;

    void send_segment(uint8_t flags, const uint8_t* payload, uint32_t len);
    uint16_t calculate_checksum(TCPHeader* header, const uint8_t* payload, uint32_t len, uint32_t src_ip, uint32_t dst_ip);
};

#endif