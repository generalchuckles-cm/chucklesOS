#ifndef XHCI_H
#define XHCI_H

#include <cstdint>
#include "../../pci/pci.h"
#include "xhci_defs.h"

struct XhciDevice {
    int slot_id;
    int port_id;
    void* input_ctx;
    void* output_ctx;
    void* ep0_ring;
    uint64_t ep0_ring_phys;
    uint64_t ep0_enqueue_ptr;
    uint32_t ep0_cycle;
};

class XhciDriver {
public:
    static XhciDriver& getInstance();
    
    bool init(uint16_t vendor_id, uint16_t device_id);
    void poll_ports();
    void poll_events();

private:
    XhciDriver();

    PCIDevice pci_dev;
    uint64_t mmio_base_virt;
    uint64_t mmio_base_phys;
    
    uint8_t cap_length;
    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t context_size_bytes; // 32 or 64
    uint64_t op_regs_base;
    uint64_t run_regs_base;
    uint64_t db_regs_base;

    uint64_t* dcbaa;        
    
    // Command Ring
    void* cmd_ring_base;
    uint64_t cmd_ring_phys;
    uint64_t cmd_ring_enqueue_ptr;
    uint32_t cmd_ring_cycle;
    
    // Event Ring
    void* event_ring_base;  
    uint64_t* erst;         
    uint32_t event_ring_cycle;
    void* event_ring_dequeue_ptr; 
    void* event_ring_segment;     

    // Devices
    XhciDevice devices[32]; // Max 32 active devices supported for now

    void write_op_reg(uint32_t offset, uint32_t val);
    uint32_t read_op_reg(uint32_t offset);
    void write_op_reg64(uint32_t offset, uint64_t val);
    
    uint32_t read_port_reg(int port, uint32_t offset);
    void write_port_reg(int port, uint32_t offset, uint32_t val);
    void ring_doorbell(int target, int stream_id);

    bool reset_controller();
    void perform_bios_handoff();
    void init_memory();
    void xhci_port_manager(); 
    void reset_port(int port);

    // Commands & Enumeration
    int send_command_and_wait(uint32_t type, uint64_t param1, uint32_t param2);
    void enumerate_device(int port_id);
    void queue_trb(void* ring_base, uint64_t& enqueue_val, uint32_t& cycle, uint64_t param, uint32_t status, uint32_t control);
};

#endif