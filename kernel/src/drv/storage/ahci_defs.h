#ifndef AHCI_DEFS_H
#define AHCI_DEFS_H

#include <cstdint>

#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35
#define ATA_CMD_IDENTIFY        0xEC

#define HBA_PORT_IPM_ACTIVE     1
#define HBA_PORT_DET_PRESENT    3

#define AHCI_DEV_NULL           0
#define AHCI_DEV_SATA           1
#define AHCI_DEV_SEMB           2
#define AHCI_DEV_PM             3
#define AHCI_DEV_SATAPI         4

#define HBA_PxCMD_ST            0x0001
#define HBA_PxCMD_FRE           0x0010
#define HBA_PxCMD_FR            0x4000
#define HBA_PxCMD_CR            0x8000

#define HBA_PxIS_TFES           (1 << 30)

// Port Registers
struct HBA_PORT {
    uint32_t clb;       // Command List Base Address
    uint32_t clbu;      // Command List Base Address Upper 32-bits
    uint32_t fb;        // FIS Base Address
    uint32_t fbu;       // FIS Base Address Upper 32-bits
    uint32_t is;        // Interrupt Status
    uint32_t ie;        // Interrupt Enable
    uint32_t cmd;       // Command and Status
    uint32_t rsv0;      // Reserved
    uint32_t tfd;       // Task File Data
    uint32_t sig;       // Signature
    uint32_t ssts;      // SATA Status (SCR0: SStatus)
    uint32_t sctl;      // SATA Control (SCR2: SControl)
    uint32_t serr;      // SATA Error (SCR1: SError)
    uint32_t sact;      // SATA Active (SCR3: SActive)
    uint32_t ci;        // Command Issue
    uint32_t sntf;      // SATA Notification (SCR4: SNotification)
    uint32_t fbs;       // FIS-based Switching Control
    uint32_t rsv1[11];  // Reserved
};

// Generic Host Control
struct HBA_MEM {
    uint32_t cap;       // Host Capabilities
    uint32_t ghc;       // Global Host Control
    uint32_t is;        // Interrupt Status
    uint32_t pi;        // Ports Implemented
    uint32_t vs;        // Version
    uint32_t ccc_ctl;   // Command Completion Coalescing Control
    uint32_t ccc_pts;   // Command Completion Coalescing Ports
    uint32_t em_loc;    // Enclosure Management Location
    uint32_t em_ctl;    // Enclosure Management Control
    uint32_t cap2;      // Host Capabilities Extended
    uint32_t bohc;      // BIOS/OS Handoff Control and Status
    uint8_t  rsv[0xA0-0x2C];
    uint8_t  vendor[0x100-0xA0];
    HBA_PORT ports[1];  // 1 ~ 32
};

struct HBA_CMD_HEADER {
    uint8_t  cfl:5;     // Command FIS Length in DWORDS, 2 ~ 16
    uint8_t  a:1;       // ATAPI
    uint8_t  w:1;       // Write, 1: H2D, 0: D2H
    uint8_t  p:1;       // Prefetchable
    uint8_t  r:1;       // Reset
    uint8_t  b:1;       // BIST
    uint8_t  c:1;       // Clear Busy upon R_OK
    uint8_t  rsv0:1;    // Reserved
    uint8_t  pmp:4;     // Port Multiplier Port
    uint16_t prdtl;     // Physical Region Descriptor Table Length in entries
    volatile uint32_t prdbc; // Physical Region Descriptor Byte Count transferred
    uint32_t ctba;      // Command Table Descriptor Base Address
    uint32_t ctbau;     // Command Table Descriptor Base Address Upper 32-bits
    uint32_t rsv1[4];   // Reserved
};

struct HBA_PRDT_ENTRY {
    uint32_t dba;       // Data Base Address
    uint32_t dbau;      // Data Base Address Upper 32-bits
    uint32_t rsv0;      // Reserved
    uint32_t dbc:22;    // Data Byte Count, 4M max
    uint32_t rsv1:9;    // Reserved
    uint32_t i:1;       // Interrupt on completion
};

struct HBA_CMD_TABLE {
    // 0x00
    uint8_t  cfis[64];  // Command FIS
    // 0x40
    uint8_t  acmd[16];  // ATAPI Command, 12 or 16 bytes
    // 0x50
    uint8_t  rsv[48];   // Reserved
    // 0x80
    HBA_PRDT_ENTRY prdt_entry[1]; // Physical Region Descriptor Table entries, 0 ~ 65535
};

struct FIS_REG_H2D {
    uint8_t  fis_type;  // FIS_TYPE_REG_H2D
    uint8_t  pmport:4;  // Port multiplier
    uint8_t  rsv0:3;    // Reserved
    uint8_t  c:1;       // 1: Command, 0: Control
    uint8_t  command;   // Command register
    uint8_t  featurel;  // Feature register, 7:0
    uint8_t  lba0;      // LBA low register, 7:0
    uint8_t  lba1;      // LBA mid register, 15:8
    uint8_t  lba2;      // LBA high register, 23:16
    uint8_t  device;    // Device register
    uint8_t  lba3;      // LBA register, 31:24
    uint8_t  lba4;      // LBA register, 39:32
    uint8_t  lba5;      // LBA register, 47:40
    uint8_t  featureh;  // Feature register, 15:8
    uint8_t  countl;    // Count register, 7:0
    uint8_t  counth;    // Count register, 15:8
    uint8_t  icc;       // Isochronous Command Completion
    uint8_t  control;   // Control register
    uint8_t  rsv1[4];   // Reserved
};

#endif