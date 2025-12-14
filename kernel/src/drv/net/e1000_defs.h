#ifndef E1000_DEFS_H
#define E1000_DEFS_H

#include <cstdint>

// Offsets
#define E1000_CTRL      0x0000  // Device Control
#define E1000_STATUS    0x0008  // Device Status
#define E1000_EERD      0x0014  // EEPROM Read
#define E1000_ICR       0x00C0  // Interrupt Cause Read
#define E1000_ICS       0x00C8  // Interrupt Cause Set
#define E1000_IMS       0x00D0  // Interrupt Mask Set
#define E1000_IMC       0x00D8  // Interrupt Mask Clear
#define E1000_RCTL      0x0100  // Receive Control
#define E1000_TCTL      0x0400  // Transmit Control
#define E1000_RDBAL     0x2800  // RX Desc Base Low
#define E1000_RDBAH     0x2804  // RX Desc Base High
#define E1000_RDLEN     0x2808  // RX Desc Length
#define E1000_RDH       0x2810  // RX Desc Head
#define E1000_RDT       0x2818  // RX Desc Tail
#define E1000_TDBAL     0x3800  // TX Desc Base Low
#define E1000_TDBAH     0x3804  // TX Desc Base High
#define E1000_TDLEN     0x3808  // TX Desc Length
#define E1000_TDH       0x3810  // TX Desc Head
#define E1000_TDT       0x3818  // TX Desc Tail
#define E1000_MTA       0x5200  // Multicast Table Array
#define E1000_RAL       0x5400  // Receive Address Low
#define E1000_RAH       0x5404  // Receive Address High

// CTRL Bits
#define E1000_CTRL_SLU  (1 << 6)  // Set Link Up
#define E1000_CTRL_RST  (1 << 26) // Device Reset

// RCTL Bits
#define E1000_RCTL_EN   (1 << 1)  // Receiver Enable
#define E1000_RCTL_SBP  (1 << 2)  // Store Bad Packets
#define E1000_RCTL_UPE  (1 << 3)  // Unicast Promiscuous
#define E1000_RCTL_MPE  (1 << 4)  // Multicast Promiscuous
#define E1000_RCTL_LPE  (1 << 5)  // Long Packet Enable
#define E1000_RCTL_BAM  (1 << 15) // Broadcast Accept Mode
#define E1000_RCTL_SECRC (1 << 26) // Strip Ethernet CRC

// TCTL Bits
#define E1000_TCTL_EN   (1 << 1)  // Transmit Enable
#define E1000_TCTL_PSP  (1 << 3)  // Pad Short Packets

// Interrupts
#define E1000_ICR_TXQE  (1 << 1)
#define E1000_ICR_LSC   (1 << 2)  // Link Status Change
#define E1000_ICR_RXT0  (1 << 7)  // Receiver Timer Interrupt

// Descriptors
struct e1000_rx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed));

#define E1000_CMD_EOP  (1 << 0) // End of Packet
#define E1000_CMD_IFCS (1 << 1) // Insert FCS (CRC)
#define E1000_CMD_RS   (1 << 3) // Report Status

#endif