#ifndef INTEL_I2C_H
#define INTEL_I2C_H

#include <cstdint>
#include "../../pci/pci.h"

// Intel DesignWare I2C Registers (Offsets)
#define IC_CON          0x00
#define IC_TAR          0x04
#define IC_DATA_CMD     0x10
#define IC_SS_SCL_HCNT  0x14
#define IC_SS_SCL_LCNT  0x18
#define IC_FS_SCL_HCNT  0x1C
#define IC_FS_SCL_LCNT  0x20
#define IC_INTR_STAT    0x2C
#define IC_INTR_MASK    0x30
#define IC_RAW_INTR_STAT 0x34
#define IC_RX_TL        0x38
#define IC_TX_TL        0x3C
#define IC_CLR_INTR     0x40
#define IC_ENABLE       0x6C
#define IC_STATUS       0x70
#define IC_TXFLR        0x74
#define IC_RXFLR        0x78
#define IC_SDA_HOLD     0x7C
#define IC_TX_ABRT_SOURCE 0x80
#define IC_ENABLE_STATUS 0x9C
#define IC_COMP_PARAM_1 0xF4

// IC_CON Bits
#define IC_CON_MASTER_MODE      (1 << 0)
#define IC_CON_SPEED_STD        (1 << 1)
#define IC_CON_SPEED_FAST       (2 << 1)
#define IC_CON_RESTART_EN       (1 << 5)
#define IC_CON_SLAVE_DISABLE    (1 << 6)

// IC_DATA_CMD Bits
#define IC_CMD_READ             (1 << 8)
#define IC_CMD_STOP             (1 << 9)
#define IC_CMD_RESTART          (1 << 10)

// IC_STATUS Bits
#define IC_STATUS_ACTIVITY      (1 << 0)
#define IC_STATUS_TFE           (1 << 2) // TX FIFO Empty
#define IC_STATUS_RFNE          (1 << 3) // RX FIFO Not Empty
#define IC_STATUS_MST_ACTIVITY  (1 << 5)

class IntelI2C {
public:
    static IntelI2C& getInstance();

    // Initializes the controller found at the specific PCI device (usually 00:15.0)
    bool init();

    // Write bytes to a slave
    bool i2c_write(uint8_t slave_addr, const uint8_t* data, uint32_t len);

    // Read bytes from a slave
    bool i2c_read(uint8_t slave_addr, uint8_t* buffer, uint32_t len);

    // Write a command/register address, then read response (Combined transaction)
    bool i2c_write_read(uint8_t slave_addr, const uint8_t* write_data, uint32_t write_len, uint8_t* read_buf, uint32_t read_len);

private:
    IntelI2C();
    
    PCIDevice pci_dev;
    uint64_t mmio_base_phys;
    uint64_t mmio_base_virt;
    
    void write_reg(uint32_t offset, uint32_t val);
    uint32_t read_reg(uint32_t offset);
    
    void enable(bool on);
    void wait_bus_not_busy();
};

#endif