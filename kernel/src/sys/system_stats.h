#ifndef SYSTEM_STATS_H
#define SYSTEM_STATS_H

#include <cstdint>

struct SystemStats {
    // Service Status Flags
    bool service_smp_active;
    bool service_e1000_active;
    bool service_ahci_active;
    bool service_xhci_active;
    bool service_ps2_active;
    
    // CPU Counters (Updated by cores)
    // We support up to 32 cores for display
    volatile uint64_t cpu_ticks[32]; 
    int cpu_count;
    
    static SystemStats& getInstance() {
        static SystemStats instance;
        return instance;
    }
    
private:
    SystemStats() : service_smp_active(false), 
                    service_e1000_active(false),
                    service_ahci_active(false),
                    service_xhci_active(false),
                    service_ps2_active(false),
                    cpu_count(1) {
        for(int i=0; i<32; i++) cpu_ticks[i] = 0;
    }
};

#endif