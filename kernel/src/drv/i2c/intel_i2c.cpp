#include "intel_i2c.h"
#include "../../memory/vmm.h"
#include "../../cppstd/stdio.h"
#include "../../timer.h"

#define INTEL_I2C_VIRT_BASE 0xFFFFA00020000000

IntelI2C::IntelI2C() : mmio_base_virt(0) {}

IntelI2C& IntelI2C::getInstance() {
    static IntelI2C instance;
    return instance;
}

void IntelI2C::write_reg(uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)(mmio_base_virt + offset) = val;
}

uint32_t IntelI2C::read_reg(uint32_t offset) {
    return *(volatile uint32_t*)(mmio_base_virt + offset);
}

void IntelI2C::enable(bool on) {
    write_reg(IC_ENABLE, on ? 1 : 0);
    // Wait for status to reflect
    int timeout = 100;
    while (timeout--) {
        uint32_t s = read_reg(IC_ENABLE_STATUS) & 1;
        if ((on && s) || (!on && !s)) return;
        sleep_ms(1);
    }
    printf("I2C: Timeout changing enable state to %d\n", on);
}

bool IntelI2C::init() {
    printf("I2C: Scanning for Intel LPSS Controller...\n");
    
    // Scan for PCI Class 0C (Serial), Subclass 80 (Other) or 05 (SMBus/I2C)
    // Intel LPSS I2C is often 00:15.0, 00:15.1, etc.
    // Device IDs vary widely (e.g. 0x31A8 for Gemini Lake, 0x9D60 for Skylake/Kabylake)
    // We search specifically for the first I2C controller we find.
    
    // Note: Use our PCI helper. If multiple exist, we might grab the wrong one for the trackpad,
    // but usually Touchpad is on I2C0 or I2C1. We'll grab the first one found.
    // 0x0C = Serial Bus Controller, 0x80 = Other (often used for these), or 0x05 = SMBus
    
    bool found = false;
    
    // Try to find specifically by Device ID for Gemini Lake J4125 (common) if generic fails
    // or just scan all serial controllers.
    
    // Let's iterate bus 0 manually for speed
    for (int dev = 0; dev < 32; dev++) {
        for (int func = 0; func < 8; func++) {
            uint16_t vid = pci_read_word(0, dev, func, 0x00);
            if (vid != 0x8086) continue;
            
            uint16_t class_sub = pci_read_word(0, dev, func, 0x0A);
            // Class 0x0C (Serial), Subclass 0x80 (Other/I2C)
            if (class_sub == 0x0C80) {
                pci_dev.bus = 0;
                pci_dev.slot = dev;
                pci_dev.function = func;
                pci_dev.vendor_id = vid;
                pci_dev.device_id = pci_read_word(0, dev, func, 0x02);
                pci_dev.bar0 = pci_read_dword(0, dev, func, 0x10) & 0xFFFFFFF0;
                // Check if this has a 64-bit BAR
                uint32_t bar0_type = pci_read_dword(0, dev, func, 0x10) & 0x6;
                if (bar0_type == 0x4) {
                    uint32_t high = pci_read_dword(0, dev, func, 0x14);
                    // We ignore high part for simple 32-bit phys assumption or merge it
                    // Assuming < 4GB phys for simplicity or correct handling:
                    // We only use bar0 low part here as per pci struct, assuming low mem.
                }
                
                // Usually Trackpad is on I2C0 or I2C1.
                // device 21 (0x15) is typical.
                if (dev == 0x15) {
                    found = true;
                    goto done_search;
                }
                // If we found another one, keep it as backup but keep searching for 0x15
                found = true; 
            }
        }
    }

done_search:
    if (!found) {
        printf("I2C: No Intel I2C Controller found.\n");
        return false;
    }

    pci_enable_bus_mastering(&pci_dev);
    
    mmio_base_phys = pci_dev.bar0;
    mmio_base_virt = INTEL_I2C_VIRT_BASE;
    
    printf("I2C: Found Dev %04x at BAR %x. Mapping...\n", pci_dev.device_id, mmio_base_phys);
    
    vmm_map_page(mmio_base_virt, mmio_base_phys, 3); // RW, Present

    // Disable to configure
    enable(false);

    // Setup Timing (Standard Mode 100kHz)
    // These values assume a 100MHz or 133MHz input clock. 
    // Conservative values derived from Linux for generic LPSS.
    write_reg(IC_SS_SCL_HCNT, 400); 
    write_reg(IC_SS_SCL_LCNT, 470);
    write_reg(IC_FS_SCL_HCNT, 60); 
    write_reg(IC_FS_SCL_LCNT, 130);
    
    write_reg(IC_CON, IC_CON_MASTER_MODE | IC_CON_SLAVE_DISABLE | IC_CON_RESTART_EN | IC_CON_SPEED_STD);
    
    // Mask Interrupts (We use polling)
    write_reg(IC_INTR_MASK, 0);
    
    // Enable
    enable(true);
    
    printf("I2C: Initialized.\n");
    return true;
}

void IntelI2C::wait_bus_not_busy() {
    int timeout = 10000;
    while (timeout--) {
        if (!(read_reg(IC_STATUS) & IC_STATUS_ACTIVITY)) return;
        asm volatile("pause");
    }
}

bool IntelI2C::i2c_write(uint8_t slave_addr, const uint8_t* data, uint32_t len) {
    enable(false);
    write_reg(IC_TAR, slave_addr);
    enable(true);
    
    for (uint32_t i = 0; i < len; i++) {
        // Wait for FIFO space
        int t = 10000;
        while ((read_reg(IC_TXFLR) > 8) && t--) asm("pause");
        
        uint32_t cmd = data[i];
        if (i == len - 1) cmd |= IC_CMD_STOP; // Stop bit on last byte
        
        write_reg(IC_DATA_CMD, cmd);
    }
    
    wait_bus_not_busy();
    
    // Check for aborts
    if (read_reg(IC_TX_ABRT_SOURCE)) {
        printf("I2C: TX Abort! Src: %x\n", read_reg(IC_TX_ABRT_SOURCE));
        read_reg(IC_CLR_INTR); // Clear interrupt
        return false;
    }
    return true;
}

bool IntelI2C::i2c_read(uint8_t slave_addr, uint8_t* buffer, uint32_t len) {
    enable(false);
    write_reg(IC_TAR, slave_addr);
    enable(true);
    
    for (uint32_t i = 0; i < len; i++) {
        uint32_t cmd = IC_CMD_READ;
        if (i == len - 1) cmd |= IC_CMD_STOP;
        
        write_reg(IC_DATA_CMD, cmd);
    }
    
    for (uint32_t i = 0; i < len; i++) {
        int t = 100000;
        while (t--) {
            if (read_reg(IC_STATUS) & IC_STATUS_RFNE) break; // RX Not Empty
            asm("pause");
        }
        if (t <= 0) return false;
        
        buffer[i] = (uint8_t)(read_reg(IC_DATA_CMD) & 0xFF);
    }
    return true;
}

bool IntelI2C::i2c_write_read(uint8_t slave_addr, const uint8_t* write_data, uint32_t write_len, uint8_t* read_buf, uint32_t read_len) {
    // 1. Setup Target
    enable(false);
    write_reg(IC_TAR, slave_addr);
    enable(true);
    
    // 2. Write Phase
    for (uint32_t i = 0; i < write_len; i++) {
        // Wait for FIFO
        int t = 10000;
        while ((read_reg(IC_TXFLR) > 8) && t--) asm("pause");
        
        uint32_t cmd = write_data[i];
        // DO NOT SEND STOP HERE, we want a RESTART for the read phase
        // However, DesignWare handles Restart automatically if we keep pushing commands
        write_reg(IC_DATA_CMD, cmd);
    }
    
    // 3. Read Phase
    for (uint32_t i = 0; i < read_len; i++) {
        uint32_t cmd = IC_CMD_READ;
        // On first read byte, ensure restart if required, though typically automatic
        if (i == 0) cmd |= IC_CMD_RESTART; 
        if (i == read_len - 1) cmd |= IC_CMD_STOP;
        
        write_reg(IC_DATA_CMD, cmd);
    }
    
    // 4. Drain RX FIFO
    for (uint32_t i = 0; i < read_len; i++) {
        int t = 100000;
        while (t--) {
            if (read_reg(IC_STATUS) & IC_STATUS_RFNE) break;
            asm("pause");
        }
        if (t <= 0) {
            printf("I2C: Read Timeout index %d\n", i);
            return false;
        }
        read_buf[i] = (uint8_t)(read_reg(IC_DATA_CMD) & 0xFF);
    }
    
    return true;
}