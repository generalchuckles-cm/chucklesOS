#ifndef NETWORK_H
#define NETWORK_H

#include <cstdint>
#include "defs.h"

// Forward declare TcpSocket to avoid circular include issues
class TcpSocket;

class NetworkStack {
public:
    static NetworkStack& getInstance();

    void init();
    
    void set_dns_server(const char* ip);
    void set_udp_speed(uint64_t speed);
    
    void handle_packet(const uint8_t* data, uint16_t len);
    
    bool send_udp(uint32_t dest_ip, uint16_t dest_port, uint16_t src_port, const void* data, uint16_t len);
    uint32_t dns_lookup(const char* hostname);
    int ping(const char* ip_str);
    
    // ARP Helper made public for TCP
    bool resolve_arp(uint32_t ip, uint8_t* mac_out);
    
    // Getters for TCP
    uint32_t get_my_ip() { return my_ip; }
    uint32_t get_gateway_ip() { return gateway_ip; }
    
    // TCP Registration
    void register_tcp_socket(TcpSocket* sock);
    void unregister_tcp_socket(TcpSocket* sock);

private:
    NetworkStack();
    
    uint32_t my_ip;      
    uint32_t gateway_ip; 
    uint32_t dns_ip;     
    uint8_t  my_mac[6];
    uint64_t max_udp_speed;

    // ARP
    uint32_t arp_target_ip;
    uint8_t  arp_result_mac[6];
    bool     arp_resolved;

    // Ping
    bool     ping_active;
    uint16_t ping_id;
    uint16_t ping_seq;
    bool     ping_reply_recvd;
    
    // DNS
    bool     dns_active;
    uint16_t dns_tx_id;
    uint32_t dns_result_ip;
    bool     dns_resolved;
    
    // Active TCP Socket (Simplification: Only one at a time for now)
    TcpSocket* active_tcp_socket;

    // Helpers
    uint32_t parse_ip(const char* str);
    void send_arp_request(uint32_t target_ip);
    uint16_t checksum(void* data, int len);
    
    void handle_udp(IPv4Header* ip, UDPHeader* udp, uint8_t* data, int len);
};

#endif