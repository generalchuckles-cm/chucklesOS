#ifndef E1000_H
#define E1000_H

#include "e1000_defs.h"
#include "../../pci/pci.h"
#include <cstdint>

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

class E1000Driver {
public:
    static E1000Driver& getInstance();

    // Init the driver
    bool init();
    
    // Reset hardware and stop RX/TX (for config reload)
    void shutdown();

    // Send an Ethernet packet
    void send_packet(const uint8_t* data, uint16_t len);

    // Called by ISR when IRQ fires
    void handle_interrupt();

    // Get MAC Address
    uint8_t* get_mac() { return mac_addr; }
    
    // Returns the IRQ line for IDT hook checks.
    // Returns 0xFF if not initialized.
    uint8_t get_irq() { return pci_dev.irq_line; }

private:
    E1000Driver();

    PCIDevice pci_dev;
    uint64_t mmio_base_virt;
    uint64_t mmio_base_phys;
    bool initialized;
    
    uint8_t mac_addr[6];
    bool has_eeprom;

    // Rings
    e1000_rx_desc* rx_descs; // Virtual
    e1000_tx_desc* tx_descs; // Virtual
    uint8_t* rx_buffers[E1000_NUM_RX_DESC]; // Virtual ptrs to buffers
    uint8_t* tx_buffers[E1000_NUM_TX_DESC];

    uint16_t rx_cur;
    uint16_t tx_cur;

    // Helpers
    void write_reg(uint32_t offset, uint32_t val);
    uint32_t read_reg(uint32_t offset);
    void detect_eeprom();
    uint16_t read_eeprom(uint32_t addr);
    void read_mac();
};

#endif