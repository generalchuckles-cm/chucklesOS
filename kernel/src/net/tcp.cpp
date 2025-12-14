#include "tcp.h"
#include "network.h"
#include "../drv/net/e1000.h"
#include "../memory/heap.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../timer.h"
#include "../gui/window.h"     
#include "../globals.h"        
#include "../input.h"          

TcpSocket::TcpSocket() : state(CLOSED), rx_head(0), rx_tail(0) {
    rx_buffer = (uint8_t*)malloc(RX_BUF_SIZE);
    local_port = 49152 + (rdtsc_serialized() % 16384);
}

// Helper to sum 16-bit words (Standard Internet Checksum logic)
static uint32_t sum_words(const void* data, int len) {
    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)data;
    for (int i = 0; i < len / 2; i++) {
        sum += ptr[i];
    }
    if (len & 1) {
        const uint8_t* b = (const uint8_t*)data;
        sum += (uint16_t)b[len-1]; 
    }
    return sum;
}

uint16_t TcpSocket::calculate_checksum(TCPHeader* header, const uint8_t* payload, uint32_t len, uint32_t src_ip, uint32_t dst_ip) {
    uint32_t sum = 0;
    
    // 1. "Pseudo" Header Fields (Summed manually)
    // Source IP (Already Big Endian)
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    
    // Dest IP (Already Big Endian)
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    
    // Protocol (6) and TCP Length
    sum += htons(6);
    sum += htons(sizeof(TCPHeader) + len);
    
    // 2. TCP Header (Checksum field must be 0 for calculation)
    header->checksum = 0;
    sum += sum_words(header, sizeof(TCPHeader));
    
    // 3. Payload
    if (payload && len > 0) {
        sum += sum_words(payload, len);
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

void TcpSocket::send_segment(uint8_t flags, const uint8_t* payload, uint32_t len) {
    uint32_t total_len = sizeof(EthernetHeader) + sizeof(IPv4Header) + sizeof(TCPHeader) + len;
    uint8_t* packet = (uint8_t*)malloc(total_len);
    if (!packet) return;
    
    memset(packet, 0, total_len);
    
    EthernetHeader* eth = (EthernetHeader*)packet;
    IPv4Header* ip = (IPv4Header*)(packet + sizeof(EthernetHeader));
    TCPHeader* tcp = (TCPHeader*)(packet + sizeof(EthernetHeader) + sizeof(IPv4Header));
    uint8_t* data_ptr = (uint8_t*)(tcp + 1);
    
    uint32_t my_ip = NetworkStack::getInstance().get_my_ip();
    uint8_t* my_mac = E1000Driver::getInstance().get_mac();
    
    uint32_t gateway = NetworkStack::getInstance().get_gateway_ip();
    uint32_t next_hop = remote_ip;
    if ((remote_ip & 0x00FFFFFF) != (my_ip & 0x00FFFFFF)) next_hop = gateway;
    
    uint8_t dest_mac[6];
    if (!NetworkStack::getInstance().resolve_arp(next_hop, dest_mac)) {
        free(packet);
        return;
    }
    
    memcpy(eth->dest, dest_mac, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = htons(ETH_TYPE_IP);
    
    ip->version = 4;
    ip->ihl = 5;
    ip->len = htons(sizeof(IPv4Header) + sizeof(TCPHeader) + len);
    ip->id = htons(0x5678);
    ip->ttl = 64;
    ip->proto = 6; 
    ip->src_ip = my_ip;
    ip->dest_ip = remote_ip;
    
    // Calculate IP Checksum
    ip->checksum = 0;
    uint32_t ip_sum = sum_words(ip, sizeof(IPv4Header));
    while(ip_sum>>16) ip_sum = (ip_sum&0xFFFF) + (ip_sum>>16);
    ip->checksum = (uint16_t)~ip_sum;
    
    tcp->src_port = htons(local_port);
    tcp->dest_port = htons(remote_port);
    tcp->seq_num = htonl(seq_num);
    tcp->ack_num = htonl(ack_num);
    tcp->data_offset = (sizeof(TCPHeader) / 4) << 4; 
    tcp->flags = flags;
    tcp->window_size = htons(8192);
    tcp->urgent_pointer = 0;
    tcp->checksum = 0;
    
    if (len > 0 && payload) memcpy(data_ptr, payload, len);
    
    tcp->checksum = calculate_checksum(tcp, data_ptr, len, my_ip, remote_ip);
    
    E1000Driver::getInstance().send_packet(packet, total_len);
    free(packet);
}

bool TcpSocket::connect(uint32_t dest_ip, uint16_t dest_port) {
    remote_ip = dest_ip;
    remote_port = dest_port;
    
    seq_num = rdtsc_serialized(); 
    ack_num = 0;
    state = SYN_SENT;
    
    NetworkStack::getInstance().register_tcp_socket(this);
    
    printf("TCP: Sending SYN to %d.%d.%d.%d:%d (Local Port %d)...\n", 
        dest_ip&0xFF, (dest_ip>>8)&0xFF, (dest_ip>>16)&0xFF, (dest_ip>>24)&0xFF, dest_port, local_port);
        
    send_segment(TCP_SYN, nullptr, 0);
    
    uint64_t start = rdtsc_serialized();
    uint64_t freq = get_cpu_frequency();
    
    // Wait for connection with visual updates
    while(state == SYN_SENT) {
        if (rdtsc_serialized() - start > freq * 5) { 
            printf("TCP: Connect Timeout.\n");
            NetworkStack::getInstance().unregister_tcp_socket(this);
            return false;
        }
        
        check_input_hooks();
        WindowManager::getInstance().update();
        if (g_renderer) WindowManager::getInstance().render(g_renderer);
    }
    
    if (state == ESTABLISHED) {
        printf("TCP: Connected!\n");
        return true;
    }
    
    return false;
}

bool TcpSocket::send(const uint8_t* data, uint32_t len) {
    if (state != ESTABLISHED) return false;
    
    send_segment(TCP_PSH | TCP_ACK, data, len);
    seq_num += len; 
    return true;
}

void TcpSocket::close() {
    if (state == ESTABLISHED) {
        send_segment(TCP_FIN | TCP_ACK, nullptr, 0);
        state = CLOSED;
    }
    NetworkStack::getInstance().unregister_tcp_socket(this);
    free(rx_buffer);
}

int TcpSocket::recv(uint8_t* buffer, uint32_t max_len) {
    uint64_t start = rdtsc_serialized();
    uint64_t freq = get_cpu_frequency();
    
    while (rx_head == rx_tail) {
        if (state == CLOSED) return -1;
        
        // 5 seconds read timeout
        if (rdtsc_serialized() - start > freq * 5) return 0; 
        
        check_input_hooks();
        WindowManager::getInstance().update();
        if (g_renderer) WindowManager::getInstance().render(g_renderer);
    }
    
    int read = 0;
    while (rx_head != rx_tail && read < (int)max_len) {
        buffer[read++] = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    }
    return read;
}

void TcpSocket::handle_packet(TCPHeader* header, uint8_t* data, uint16_t len) {
    uint8_t flags = header->flags;
    uint32_t seq = ntohl(header->seq_num);
    uint32_t ack = ntohl(header->ack_num);
    
    printf("TCP RX: Flags %x Len %d Seq %u Ack %u\n", flags, len, seq, ack);

    if (state == SYN_SENT) {
        if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
            ack_num = seq + 1;
            seq_num = ack; 
            
            // Send ACK to complete handshake
            send_segment(TCP_ACK, nullptr, 0);
            state = ESTABLISHED;
        }
    }
    else if (state == ESTABLISHED) {
        if (flags & TCP_FIN) {
            ack_num++;
            send_segment(TCP_ACK, nullptr, 0);
            state = CLOSED;
            return;
        }
        
        // Handle Payload
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                int next = (rx_head + 1) % RX_BUF_SIZE;
                if (next != rx_tail) { 
                    rx_buffer[rx_head] = data[i];
                    rx_head = next;
                }
            }
            
            ack_num += len;
            send_segment(TCP_ACK, nullptr, 0);
        }
    }
}