#ifndef CHUCKLES_DAEMON_H
#define CHUCKLES_DAEMON_H

class ChucklesDaemon {
public:
    static ChucklesDaemon& getInstance();
    
    // Reads /dns.cfg and /udp.cfg, restarts E1000 and NetworkStack
    void reload_network_config();

private:
    ChucklesDaemon() {}
    
    void load_dns_config();
    void load_udp_config();
};

#endif