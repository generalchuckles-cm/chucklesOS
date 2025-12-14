#ifndef NET_DEFS_H
#define NET_DEFS_H

#include <cstdint>

#define ETH_TYPE_IP  0x0800
#define ETH_TYPE_ARP 0x0806

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

#define DNS_PORT 53

struct EthernetHeader {
    uint8_t dest[6];
    uint8_t src[6];
    uint16_t type; // Big Endian
} __attribute__((packed));

struct ARPHeader {
    uint16_t hw_type;    // 1 = Ethernet
    uint16_t proto_type; // 0x0800 = IP
    uint8_t  hw_len;     // 6
    uint8_t  proto_len;  // 4
    uint16_t opcode;     // 1 = Req, 2 = Reply
    uint8_t  src_mac[6];
    uint32_t src_ip;     // Big Endian
    uint8_t  dest_mac[6];
    uint32_t dest_ip;    // Big Endian
} __attribute__((packed));

struct IPv4Header {
    uint8_t  ihl : 4;
    uint8_t  version : 4;
    uint8_t  tos;
    uint16_t len;        // Total Length
    uint16_t id;
    uint16_t frag_offset;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed));

struct ICMPHeader {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

struct UDPHeader {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct DNSHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t q_count;
    uint16_t ans_count;
    uint16_t auth_count;
    uint16_t add_count;
} __attribute__((packed));

// Byte Swapping
static inline uint16_t htons(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }

static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | 
           ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

#endif