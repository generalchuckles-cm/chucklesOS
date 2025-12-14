#ifndef XHCI_DEFS_H
#define XHCI_DEFS_H

#include <cstdint>

// Capability Registers Offsets
#define XHCI_CAPLENGTH      0x00
#define XHCI_HCIVERSION     0x02
#define XHCI_HCSPARAMS1     0x04
#define XHCI_HCSPARAMS2     0x08
#define XHCI_HCSPARAMS3     0x0C
#define XHCI_HCCPARAMS1     0x10
#define XHCI_DBOFF          0x14
#define XHCI_RTSOFF         0x18
#define XHCI_HCCPARAMS2     0x1C

// Doorbell Offset
#define XHCI_DOORBELL_OFFSET 0x00

// Extended Capabilities
#define XHCI_LEGACY_ID            1
#define XHCI_LEGACY_BIOS_OWNED    (1 << 16)
#define XHCI_LEGACY_OS_OWNED      (1 << 24)

// Operational Registers
#define XHCI_USBCMD         0x00
#define XHCI_USBSTS         0x04
#define XHCI_PAGESIZE       0x08
#define XHCI_DNCTRL         0x14
#define XHCI_CRCR           0x18 
#define XHCI_DCBAAP         0x30 
#define XHCI_CONFIG         0x38

#define XHCI_PORTSC_OFFSET  0x400

// Runtime Registers
#define XHCI_MFINDEX        0x00
#define XHCI_IMAN(n)        (0x20 + (n * 32))
#define XHCI_IMOD(n)        (0x24 + (n * 32))
#define XHCI_ERSTSZ(n)      (0x28 + (n * 32))
#define XHCI_ERSTBA(n)      (0x30 + (n * 32))
#define XHCI_ERDP(n)        (0x38 + (n * 32))

// USBCMD Bits
#define USBCMD_RUN          (1 << 0)
#define USBCMD_RESET        (1 << 1)
#define USBCMD_INTE         (1 << 2)

// USBSTS Bits
#define USBSTS_HCH          (1 << 0)
#define USBSTS_CNR          (1 << 11)

// Port Status Bits
#define PORT_CONNECT    (1 << 0)
#define PORT_PE         (1 << 1)
#define PORT_OC         (1 << 3)
#define PORT_RESET      (1 << 4)
#define PORT_POWER      (1 << 9)
#define PORT_LSTATUS_MASK (0xF << 5)
#define PORT_CSC        (1 << 17)
#define PORT_PEC        (1 << 18)
#define PORT_WRC        (1 << 19)
#define PORT_OCC        (1 << 20)
#define PORT_RC         (1 << 21)
#define PORT_PLC        (1 << 22)
#define PORT_CEC        (1 << 23)
#define PORT_CHANGE_MASK (PORT_CSC | PORT_PEC | PORT_WRC | PORT_OCC | PORT_RC | PORT_PLC | PORT_CEC)

// TRB Types
#define TRB_NORMAL          1
#define TRB_SETUP_STAGE     2
#define TRB_DATA_STAGE      3
#define TRB_STATUS_STAGE    4
#define TRB_LINK            6
#define TRB_ENABLE_SLOT     9
#define TRB_ADDRESS_DEVICE  11
#define TRB_CONFIG_EP       12
#define TRB_NOOP            23
#define TRB_TRANSFER_EVENT  32
#define TRB_CMD_COMPLETION  33
#define TRB_PORT_STATUS     34

// USB Standard Requests
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_DESC_DEVICE        0x01
#define USB_DESC_STRING        0x03

struct XhciTrb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

struct XhciEventTrb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

// xHCI Context Structures (Assuming 32-byte context size for simplicity in typedef, but we allocate based on CSZ)
struct XhciSlotContext {
    uint32_t info1;
    uint32_t info2;
    uint32_t tt_info;
    uint32_t state;
    uint32_t reserved[4];
} __attribute__((packed));

struct XhciEpContext {
    uint32_t info1;
    uint32_t info2;
    uint64_t deq_ptr;
    uint32_t tx_info;
    uint32_t reserved[3];
} __attribute__((packed));

struct XhciInputControlContext {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
} __attribute__((packed));

// USB Device Descriptor
struct UsbDeviceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

#endif