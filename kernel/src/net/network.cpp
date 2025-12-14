#include "network.h"
#include "tcp.h" 
#include "../drv/net/e1000.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../timer.h"
#include "../memory/heap.h" 
#include "../gui/window.h" // Added for UI updates
#include "../globals.h"     // Added for g_renderer
#include "../input.h"       // Added for check_input_hooks

NetworkStack::NetworkStack() : arp_resolved(false), ping_active(false), dns_active(false), active_tcp_socket(nullptr) {
    my_ip = htonl((10 << 24) | (0 << 16) | (2 << 8) | 15);
    gateway_ip = htonl((10 << 24) | (0 << 16) | (2 << 8) | 2);
    dns_ip = htonl((10 << 24) | (0 << 16) | (2 << 8) | 3);
    max_udp_speed = 0;
}

NetworkStack& NetworkStack::getInstance() {
    static NetworkStack instance;
    return instance;
}

void NetworkStack::init() {
    uint8_t* m = E1000Driver::getInstance().get_mac();
    memcpy(my_mac, m, 6);
    printf("NET: Stack Up. IP: 10.0.2.15 GW: 10.0.2.2 DNS: %d.%d.%d.%d\n",
        (dns_ip) & 0xFF, (dns_ip >> 8) & 0xFF, (dns_ip >> 16) & 0xFF, (dns_ip >> 24) & 0xFF);
}

void NetworkStack::register_tcp_socket(TcpSocket* sock) {
    active_tcp_socket = sock;
}

void NetworkStack::unregister_tcp_socket(TcpSocket* sock) {
    if (active_tcp_socket == sock) active_tcp_socket = nullptr;
}

void NetworkStack::set_dns_server(const char* ip) {
    if (ip && ip[0]) {
        dns_ip = parse_ip(ip);
        printf("NET: DNS Server set to %s\n", ip);
    }
}

void NetworkStack::set_udp_speed(uint64_t speed) {
    max_udp_speed = speed;
    printf("NET: Max UDP Speed set to %d\n", (int)speed);
}

uint32_t NetworkStack::parse_ip(const char* str) {
    uint8_t parts[4] = {0,0,0,0};
    int p = 0;
    int val = 0;
    bool has_dot = false;
    while (*str) {
        if (*str == '.') {
            parts[p++] = val;
            val = 0;
            has_dot = true;
        } else if (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str - '0');
        } else {
            return 0; 
        }
        str++;
    }
    parts[p] = val;
    if (!has_dot) return 0; 
    return htonl((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
}

uint16_t NetworkStack::checksum(void* vdata, int len) {
    uint16_t* data = (uint16_t*)vdata;
    uint32_t sum = 0;
    for (int i = 0; i < len / 2; i++) {
        sum += data[i];
    }
    if (len & 1) {
        sum += ((uint8_t*)vdata)[len-1];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void NetworkStack::send_arp_request(uint32_t target_ip) {
    uint8_t packet[42];
    EthernetHeader* eth = (EthernetHeader*)packet;
    ARPHeader* arp = (ARPHeader*)(packet + sizeof(EthernetHeader));

    memset(eth->dest, 0xFF, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_ARP);

    arp->hw_type = htons(1);
    arp->proto_type = htons(0x0800);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->opcode = htons(ARP_OP_REQUEST);
    memcpy(arp->src_mac, my_mac, 6);
    arp->src_ip = my_ip;
    memset(arp->dest_mac, 0, 6);
    arp->dest_ip = target_ip;

    E1000Driver::getInstance().send_packet(packet, 42);
}

bool NetworkStack::resolve_arp(uint32_t ip, uint8_t* mac_out) {
    arp_resolved = false;
    arp_target_ip = ip;
    send_arp_request(ip);

    uint64_t start = rdtsc_serialized();
    uint64_t freq = get_cpu_frequency();
    
    while (true) {
        if (arp_resolved) {
            memcpy(mac_out, arp_result_mac, 6);
            return true;
        }
        if (rdtsc_serialized() - start > freq * 2) break;
        
        // Keep UI alive while waiting
        check_input_hooks();
        WindowManager::getInstance().update();
        if (g_renderer) WindowManager::getInstance().render(g_renderer);
    }
    return false;
}

bool NetworkStack::send_udp(uint32_t dest_ip, uint16_t dest_port, uint16_t src_port, const void* data, uint16_t len) {
    uint32_t next_hop = dest_ip;
    if ((dest_ip & 0x00FFFFFF) != (my_ip & 0x00FFFFFF)) next_hop = gateway_ip;

    uint8_t dest_mac[6];
    if (!resolve_arp(next_hop, dest_mac)) return false;

    uint32_t total_len = sizeof(EthernetHeader) + sizeof(IPv4Header) + sizeof(UDPHeader) + len;
    uint8_t* packet = (uint8_t*)malloc(total_len); 
    if (!packet) return false;
    
    memset(packet, 0, total_len);

    EthernetHeader* eth = (EthernetHeader*)packet;
    IPv4Header* ip = (IPv4Header*)(packet + sizeof(EthernetHeader));
    UDPHeader* udp = (UDPHeader*)(packet + sizeof(EthernetHeader) + sizeof(IPv4Header));
    uint8_t* payload = (uint8_t*)(udp + 1);

    memcpy(eth->dest, dest_mac, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_IP);

    ip->version = 4;
    ip->ihl = 5;
    ip->len = htons(sizeof(IPv4Header) + sizeof(UDPHeader) + len);
    ip->id = htons(0x1234);
    ip->ttl = 64;
    ip->proto = IP_PROTO_UDP;
    ip->src_ip = my_ip;
    ip->dest_ip = dest_ip;
    ip->checksum = checksum(ip, sizeof(IPv4Header));

    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(sizeof(UDPHeader) + len);
    udp->checksum = 0; 

    memcpy(payload, data, len);

    E1000Driver::getInstance().send_packet(packet, total_len);
    free(packet);
    return true;
}

void NetworkStack::handle_packet(const uint8_t* data, uint16_t len) {
    if (len < sizeof(EthernetHeader)) return;
    EthernetHeader* eth = (EthernetHeader*)data;
    uint16_t type = ntohs(eth->type);

    if (type == ETH_TYPE_ARP) {
        ARPHeader* arp = (ARPHeader*)(data + sizeof(EthernetHeader));
        if (ntohs(arp->opcode) == ARP_OP_REPLY) {
            if (arp->src_ip == arp_target_ip) {
                memcpy(arp_result_mac, arp->src_mac, 6);
                arp_resolved = true;
            }
        }
    }
    else if (type == ETH_TYPE_IP) {
        IPv4Header* ip = (IPv4Header*)(data + sizeof(EthernetHeader));
        if (ip->dest_ip == my_ip) {
            
            // printf("IP Packet: Proto %d Src %x\n", ip->proto, ip->src_ip);

            int ip_hdr_len = (ip->ihl) * 4;
            
            if (ip->proto == IP_PROTO_ICMP) {
                ICMPHeader* icmp = (ICMPHeader*)(data + sizeof(EthernetHeader) + ip_hdr_len);
                if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
                    if (ping_active && ntohs(icmp->id) == ping_id && ntohs(icmp->seq) == ping_seq) {
                        ping_reply_recvd = true;
                    }
                }
            }
            else if (ip->proto == IP_PROTO_UDP) {
                UDPHeader* udp = (UDPHeader*)(data + sizeof(EthernetHeader) + ip_hdr_len);
                uint8_t* payload = (uint8_t*)(udp + 1);
                int udp_len = ntohs(udp->length) - sizeof(UDPHeader);
                handle_udp(ip, udp, payload, udp_len);
            }
            else if (ip->proto == 6) { // TCP
                TCPHeader* tcp = (TCPHeader*)(data + sizeof(EthernetHeader) + ip_hdr_len);
                if (active_tcp_socket) {
                    if (ntohs(tcp->dest_port) == active_tcp_socket->get_local_port()) {
                        uint32_t tcp_hdr_len = (tcp->data_offset >> 4) * 4;
                        uint8_t* tcp_payload = (uint8_t*)tcp + tcp_hdr_len;
                        int tcp_payload_len = ntohs(ip->len) - ip_hdr_len - tcp_hdr_len;
                        
                        active_tcp_socket->handle_packet(tcp, tcp_payload, tcp_payload_len);
                    }
                }
            }
        }
    }
}

void NetworkStack::handle_udp(IPv4Header* ip, UDPHeader* udp, uint8_t* data, int len) {
    (void)ip;
    (void)len; 
    if (ntohs(udp->src_port) == 53 && dns_active) {
        DNSHeader* dns = (DNSHeader*)data;
        if (ntohs(dns->id) == dns_tx_id) {
            uint8_t* ptr = data + sizeof(DNSHeader);
            while (*ptr != 0) ptr++; ptr++; 
            ptr += 4; 
            if ((*ptr & 0xC0) == 0xC0) ptr += 2;
            else { while (*ptr != 0) ptr++; ptr++; }
            uint16_t type = ntohs(*(uint16_t*)ptr); ptr += 2;
            ptr += 2; ptr += 4; 
            uint16_t rdlen = ntohs(*(uint16_t*)ptr); ptr += 2;
            if (type == 1 && rdlen == 4) { 
                dns_result_ip = *(uint32_t*)ptr; 
                dns_resolved = true;
            }
        }
    }
}

uint32_t NetworkStack::dns_lookup(const char* hostname) {
    uint32_t direct_ip = parse_ip(hostname);
    if (direct_ip != 0) return direct_ip;

    uint8_t buf[512];
    memset(buf, 0, 512);
    DNSHeader* dns = (DNSHeader*)buf;
    dns_tx_id = 0xABCD;
    dns->id = htons(dns_tx_id);
    dns->flags = htons(0x0100); 
    dns->q_count = htons(1);
    
    uint8_t* qname = buf + sizeof(DNSHeader);
    const char* reader = hostname;
    uint8_t* label_len = qname++;
    int cnt = 0;
    while (*reader) {
        if (*reader == '.') {
            *label_len = cnt;
            label_len = qname++;
            cnt = 0;
        } else {
            *qname++ = *reader;
            cnt++;
        }
        reader++;
    }
    *label_len = cnt;
    *qname++ = 0; 
    
    *(uint16_t*)qname = htons(1); qname += 2; 
    *(uint16_t*)qname = htons(1); qname += 2; 
    
    int len = qname - buf;
    
    dns_active = true;
    dns_resolved = false;
    
    printf("NET: Querying DNS %d.%d.%d.%d for %s...\n", 
        dns_ip & 0xFF, (dns_ip >> 8) & 0xFF, (dns_ip >> 16) & 0xFF, (dns_ip >> 24) & 0xFF, hostname);
    
    if (!send_udp(dns_ip, 53, 50000, buf, len)) return 0;
    
    uint64_t start = rdtsc_serialized();
    uint64_t freq = get_cpu_frequency();
    while (true) {
        if (dns_resolved) {
            dns_active = false;
            return dns_result_ip;
        }
        if (rdtsc_serialized() - start > freq * 3) {
            printf("NET: DNS Timeout.\n");
            dns_active = false;
            return 0;
        }
        
        // Keep UI alive while waiting
        check_input_hooks();
        WindowManager::getInstance().update();
        if (g_renderer) WindowManager::getInstance().render(g_renderer);
    }
}

int NetworkStack::ping(const char* ip_str) {
    uint32_t target_ip = parse_ip(ip_str);
    if (target_ip == 0) {
        printf("NET: Invalid IP format.\n");
        return -1;
    }

    uint32_t next_hop_ip = target_ip;
    if ((target_ip & 0x00FFFFFF) != (my_ip & 0x00FFFFFF)) next_hop_ip = gateway_ip;

    uint8_t dest_mac[6];
    if (!resolve_arp(next_hop_ip, dest_mac)) {
        printf("NET: Host unreachable.\n");
        return -1;
    }

    uint8_t packet[128];
    memset(packet, 0, 128);
    EthernetHeader* eth = (EthernetHeader*)packet;
    IPv4Header* ip = (IPv4Header*)(packet + sizeof(EthernetHeader));
    ICMPHeader* icmp = (ICMPHeader*)(packet + sizeof(EthernetHeader) + sizeof(IPv4Header));
    
    memcpy(eth->dest, dest_mac, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_IP);
    
    ip->version = 4;
    ip->ihl = 5;
    ip->len = htons(sizeof(IPv4Header) + sizeof(ICMPHeader));
    ip->id = htons(1);
    ip->ttl = 64;
    ip->proto = IP_PROTO_ICMP;
    ip->src_ip = my_ip;
    ip->dest_ip = target_ip;
    ip->checksum = checksum(ip, sizeof(IPv4Header));
    
    ping_id = 0x1234;
    ping_seq = 1;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->id = htons(ping_id);
    icmp->seq = htons(ping_seq);
    icmp->checksum = checksum(icmp, sizeof(ICMPHeader));

    ping_active = true;
    ping_reply_recvd = false;
    uint64_t start_time = rdtsc_serialized();
    uint64_t cpu_freq = get_cpu_frequency();
    
    E1000Driver::getInstance().send_packet(packet, sizeof(EthernetHeader) + sizeof(IPv4Header) + sizeof(ICMPHeader));
    
    while (true) {
        if (ping_reply_recvd) {
            uint64_t end_time = rdtsc_serialized();
            ping_active = false;
            return (int)((end_time - start_time) / (cpu_freq / 1000));
        }
        if (rdtsc_serialized() - start_time > cpu_freq) {
            ping_active = false;
            return -1;
        }
        
        // Keep UI alive
        check_input_hooks();
        WindowManager::getInstance().update();
        if (g_renderer) WindowManager::getInstance().render(g_renderer);
    }
}