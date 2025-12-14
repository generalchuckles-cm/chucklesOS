#include "chuckles_daemon.h"
#include "../fs/fat32.h"
#include "../net/network.h"
#include "../drv/net/e1000.h"
#include "../cppstd/stdio.h"
#include "../cppstd/stdlib.h"
#include "../memory/heap.h"

ChucklesDaemon& ChucklesDaemon::getInstance() {
    static ChucklesDaemon instance;
    return instance;
}

void ChucklesDaemon::load_dns_config() {
    char buf[64];
    if (Fat32::getInstance().read_file("dns.cfg", buf, 64)) {
        // Simple trim of newline
        for(int i=0; i<64; i++) if(buf[i] == '\n' || buf[i] == '\r') buf[i] = 0;
        NetworkStack::getInstance().set_dns_server(buf);
    } else {
        printf("DAEMON: /dns.cfg not found. Using default.\n");
    }
}

void ChucklesDaemon::load_udp_config() {
    char buf[64];
    if (Fat32::getInstance().read_file("udp.cfg", buf, 64)) {
        // Parse int
        int speed = 0;
        char* ptr = buf;
        while(*ptr >= '0' && *ptr <= '9') {
            speed = speed * 10 + (*ptr - '0');
            ptr++;
        }
        NetworkStack::getInstance().set_udp_speed(speed);
    }
}

void ChucklesDaemon::reload_network_config() {
    printf("DAEMON: Reloading Network Configuration...\n");
    
    // 1. Shutdown Devices
    E1000Driver::getInstance().shutdown();
    
    // 2. Load Configs from Disk
    load_dns_config();
    load_udp_config();
    
    // 3. Restart Devices
    if (E1000Driver::getInstance().init()) {
        printf("DAEMON: Network Reloaded Successfully.\n");
    } else {
        printf("DAEMON: Network Restart Failed.\n");
    }
}