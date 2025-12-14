#ifndef PCI_H
#define PCI_H

#include <cstdint>
#include "../io.h"
#include "../cppstd/stdio.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

struct PCIDevice {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass_id;
    uint8_t prog_if;
    uint32_t bar0; 
    uint8_t irq_line; // Interrupt Line (Offset 0x3C)
};

static inline uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outb_32(PCI_CONFIG_ADDRESS, address);
    return (uint16_t)((inb_32(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

static inline uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outb_32(PCI_CONFIG_ADDRESS, address);
    return inb_32(PCI_CONFIG_DATA);
}

static inline void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outb_32(PCI_CONFIG_ADDRESS, address);
    outb_32(PCI_CONFIG_DATA, value);
}

static inline uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outb_32(PCI_CONFIG_ADDRESS, address);
    return (uint8_t)((inb_32(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
}

inline bool pci_find_gpu(PCIDevice* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read_word(bus, slot, 0, 0);
            if (vendor == 0xFFFF) continue;
            uint16_t class_sub = pci_read_word(bus, slot, 0, 0x0A);
            uint8_t class_id = (class_sub >> 8) & 0xFF;
            if (class_id == 0x03) {
                out->bus = (uint8_t)bus;
                out->slot = slot;
                out->function = 0;
                out->vendor_id = vendor;
                out->device_id = pci_read_word(bus, slot, 0, 2);
                out->class_id = class_id;
                out->subclass_id = class_sub & 0xFF;
                out->bar0 = pci_read_dword(bus, slot, 0, 0x10) & 0xFFFFFFF0;
                out->irq_line = pci_read_byte(bus, slot, 0, 0x3C);
                return true;
            }
        }
    }
    return false;
}

// RESTORED FUNCTION
inline bool pci_find_device_by_class(uint8_t class_id, uint8_t subclass_id, PCIDevice* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read_word(bus, slot, 0, 0);
            if (vendor == 0xFFFF) continue;

            uint16_t class_sub = pci_read_word(bus, slot, 0, 0x0A);
            uint8_t dev_class = (class_sub >> 8) & 0xFF;
            uint8_t dev_subclass = class_sub & 0xFF;

            if (dev_class == class_id && dev_subclass == subclass_id) {
                out->bus = (uint8_t)bus;
                out->slot = slot;
                out->function = 0;
                out->vendor_id = vendor;
                out->device_id = pci_read_word(bus, slot, 0, 2);
                out->class_id = dev_class;
                out->subclass_id = dev_subclass;
                out->bar0 = pci_read_dword(bus, slot, 0, 0x10) & 0xFFFFFFF0;
                out->irq_line = pci_read_byte(bus, slot, 0, 0x3C); // Capture IRQ line
                return true;
            }
        }
    }
    return false;
}

inline void pci_enable_bus_mastering(PCIDevice* dev) {
    uint32_t val = pci_read_dword(dev->bus, dev->slot, dev->function, 0x04);
    
    // Bit 2: Bus Master Enable
    // Bit 10: Interrupt Disable (1=Disabled, 0=Enabled). 
    // WE MUST CLEAR BIT 10 TO ALLOW LEGACY INTERRUPTS!
    
    val |= (1 << 2);      // Set Bus Master
    val &= ~(1 << 10);    // Clear Interrupt Disable (Enable INTx)

    pci_write_dword(dev->bus, dev->slot, dev->function, 0x04, val);
    printf("PCI: Enabled Bus Master & INTx for %02x:%02x (IRQ Line: %d)\n", 
           dev->bus, dev->slot, dev->irq_line);
}

#endif