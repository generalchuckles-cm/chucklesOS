#include "xhci.h"
#include "xhci_defs.h"
#include "../../cppstd/stdio.h"
#include "../../cppstd/string.h"
#include "../../memory/vmm.h"
#include "../../memory/pmm.h"
#include "../../timer.h"
#include "../../io.h"
#include "../../input.h" 
#include "../../interrupts/pic.h"
#include "../../globals.h"

#define XHCI_VIRT_BASE 0xFFFFA00000000000ULL

XhciDriver::XhciDriver() : event_ring_cycle(1), event_ring_dequeue_ptr(nullptr), event_ring_segment(nullptr) {
}

XhciDriver& XhciDriver::getInstance() {
    static XhciDriver instance;
    return instance;
}

static void* alloc_aligned(size_t size, size_t alignment) {
    (void)alignment; // PMM gives 4096 alignment, which covers 16, 32, 64 requirements
    size_t pages = (size + 4095) / 4096;
    void* phys = pmm_alloc(pages);
    if (!phys) return nullptr;
    return (void*)((uint64_t)phys + g_hhdm_offset);
}

static uint64_t get_phys(void* virt) {
    return (uint64_t)virt - g_hhdm_offset;
}

// --- Register Access ---

void XhciDriver::write_op_reg(uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)(op_regs_base + offset) = val;
}

uint32_t XhciDriver::read_op_reg(uint32_t offset) {
    return *(volatile uint32_t*)(op_regs_base + offset);
}

void XhciDriver::write_op_reg64(uint32_t offset, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    write_op_reg(offset, lo);
    write_op_reg(offset + 4, hi);
}

uint32_t XhciDriver::read_port_reg(int port, uint32_t offset) {
    uint64_t addr = op_regs_base + XHCI_PORTSC_OFFSET + ((port - 1) * 0x10) + offset;
    return *(volatile uint32_t*)addr;
}

void XhciDriver::write_port_reg(int port, uint32_t offset, uint32_t val) {
    uint64_t addr = op_regs_base + XHCI_PORTSC_OFFSET + ((port - 1) * 0x10) + offset;
    *(volatile uint32_t*)addr = val;
}

void XhciDriver::ring_doorbell(int target, int stream_id) {
    *(volatile uint32_t*)(db_regs_base + (target * 4)) = (stream_id << 16) | (target & 0xFF);
}

void XhciDriver::perform_bios_handoff() {
    printf("XHCI: BIOS Handoff...\n");
    uint32_t hccparams1 = *(volatile uint32_t*)(mmio_base_virt + XHCI_HCCPARAMS1);
    uint32_t xecp_offset = (hccparams1 >> 16) & 0xFFFF;
    if (xecp_offset == 0) return;

    uint64_t cur_ptr = mmio_base_virt + (xecp_offset << 2);
    while (true) {
        uint32_t cap_header = *(volatile uint32_t*)cur_ptr;
        if ((cap_header & 0xFF) == XHCI_LEGACY_ID) {
            *(volatile uint32_t*)cur_ptr |= XHCI_LEGACY_OS_OWNED;
            int t = 2000;
            while ((*(volatile uint32_t*)cur_ptr & XHCI_LEGACY_BIOS_OWNED) && t--) sleep_ms(1);
            return;
        }
        if (((cap_header >> 8) & 0xFF) == 0) break;
        cur_ptr += ((cap_header >> 8) & 0xFF) << 2;
    }
}

// --- COMMAND RING HELPERS ---

void XhciDriver::queue_trb(void* ring_base, uint64_t& enqueue_offset, uint32_t& cycle, uint64_t param, uint32_t status, uint32_t control) {
    XhciTrb* trb = (XhciTrb*)((uint64_t)ring_base + enqueue_offset);
    
    trb->param = param;
    trb->status = status;
    trb->control = control | (cycle & 1); // Set Cycle Bit
    
    enqueue_offset += 16;
    
    // Simple ring wrap handling
    if (enqueue_offset >= 4096 - 16) { 
        // For this bootloader-style driver, we assume we won't wrap ring during init.
        // If we do, we'd need a Link TRB.
        enqueue_offset = 0;
        cycle ^= 1;
    }
}

int XhciDriver::send_command_and_wait(uint32_t type, uint64_t param1, uint32_t param2) {
    // 1. Queue the TRB
    queue_trb(cmd_ring_base, cmd_ring_enqueue_ptr, cmd_ring_cycle, param1, param2, (type << 10));
    
    // 2. Ring Doorbell (Target 0 = Host Controller)
    ring_doorbell(0, 0);
    
    // 3. Poll Event Ring for Completion
    uint64_t timeout = 1000; // 1 second timeout
    while(timeout--) {
        XhciEventTrb* evt = (XhciEventTrb*)event_ring_dequeue_ptr;
        
        // Check if Cycle bit matches
        if ((evt->control & 1) == event_ring_cycle) {
            uint32_t trb_type = (evt->control >> 10) & 0x3F;
            
            // Advance Ring
            event_ring_dequeue_ptr = (void*)((uint64_t)event_ring_dequeue_ptr + 16);
            
            uint64_t erdp_val = get_phys(event_ring_dequeue_ptr);
            *(volatile uint32_t*)(run_regs_base + XHCI_ERDP(0)) = (uint32_t)erdp_val | 8; 
            *(volatile uint32_t*)(run_regs_base + XHCI_ERDP(0) + 4) = (uint32_t)(erdp_val >> 32);

            // Clear IP
            uint32_t iman = *(volatile uint32_t*)(run_regs_base + XHCI_IMAN(0));
            if (iman & 1) *(volatile uint32_t*)(run_regs_base + XHCI_IMAN(0)) = iman | 1;

            if (trb_type == TRB_CMD_COMPLETION) {
                uint32_t completion_code = (evt->status >> 24) & 0xFF;
                if (completion_code != 1) { // 1 = Success
                    printf("XHCI: Cmd Failed (Code %d)\n", completion_code);
                    return -1;
                }
                // Return Slot ID
                return (evt->control >> 24) & 0xFF;
            }
        }
        sleep_ms(1);
    }
    printf("XHCI: Command Timeout\n");
    return -1;
}

// --- ENUMERATION ---

void XhciDriver::enumerate_device(int port_id) {
    printf("XHCI: Enumerating Port %d...\n", port_id);
    
    // 1. Enable Slot
    int slot_id = send_command_and_wait(TRB_ENABLE_SLOT, 0, 0);
    if (slot_id < 0) return;
    
    printf("XHCI: Acquired Slot ID %d\n", slot_id);
    
    XhciDevice* dev = &devices[slot_id];
    dev->slot_id = slot_id;
    dev->port_id = port_id;
    
    // 2. Alloc Contexts
    dev->output_ctx = alloc_aligned(context_size_bytes * 32, 64);
    dcbaa[slot_id] = get_phys(dev->output_ctx);
    
    dev->input_ctx = alloc_aligned(context_size_bytes * 33, 64); 
    
    dev->ep0_ring = alloc_aligned(4096, 64);
    dev->ep0_ring_phys = get_phys(dev->ep0_ring);
    dev->ep0_enqueue_ptr = 0;
    dev->ep0_cycle = 1;
    
    // 3. Fill Input Context
    uint8_t* icc = (uint8_t*)dev->input_ctx; 
    uint8_t* sc = icc + context_size_bytes;  
    uint8_t* ep0 = sc + context_size_bytes;  
    
    // ICC: Add Slot(0) and EP0(1)
    *(uint32_t*)(icc + 4) = 0x3; 
    
    // Slot Context
    uint32_t portsc = read_port_reg(port_id, 0);
    uint32_t speed = (portsc >> 10) & 0xF;
    
    // Context Entries=1, Speed, Root Hub Port Number
    *(uint32_t*)(sc) = (1 << 27) | (speed << 20);
    *(uint32_t*)(sc + 4) = (port_id << 16); // Must be 1-based port index
    
    // EP0 Context
    int max_packet = (speed == 4) ? 512 : 64; 
    // Type=Control(4), MaxPSize, CErr=3
    *(uint32_t*)(ep0 + 4) = (3 << 1) | (4 << 3) | (max_packet << 16);
    *(uint64_t*)(ep0 + 16) = dev->ep0_ring_phys | 1; 
    
    // 4. Address Device
    printf("XHCI: Addressing Device...\n");
    int res = send_command_and_wait(TRB_ADDRESS_DEVICE, get_phys(dev->input_ctx), (slot_id << 24));
    if (res != slot_id) {
        printf("XHCI: Address Failed (Code %d).\n", res);
        return;
    }
    printf("XHCI: Device Addressed.\n");
    
    // 5. Read Descriptor (Get Descriptor)
    // Setup Packet: 80 06 00 01 00 00 12 00
    queue_trb(dev->ep0_ring, dev->ep0_enqueue_ptr, dev->ep0_cycle, 0x0012010000000680UL, 8, 
              (TRB_SETUP_STAGE << 10) | (2 << 16) | (3 << 6)); 
              
    void* buffer = alloc_aligned(64, 64);
    queue_trb(dev->ep0_ring, dev->ep0_enqueue_ptr, dev->ep0_cycle, get_phys(buffer), 18,
              (TRB_DATA_STAGE << 10) | (1 << 16)); 
              
    queue_trb(dev->ep0_ring, dev->ep0_enqueue_ptr, dev->ep0_cycle, 0, 0,
              (TRB_STATUS_STAGE << 10) | (1 << 5)); 
              
    ring_doorbell(slot_id, 1); 
    
    sleep_ms(100); // Wait for transfer
    
    UsbDeviceDescriptor* desc = (UsbDeviceDescriptor*)buffer;
    printf("XHCI: DEVICE IDENTIFIED:\n");
    printf("  VID: %04x  PID: %04x\n", desc->idVendor, desc->idProduct);
    if (desc->bDeviceClass == 0) printf("  Type: Interface Specific (Keyboard/Mouse?)\n");
    else if (desc->bDeviceClass == 9) printf("  Type: Hub\n");
    else printf("  Type: Class %d\n", desc->bDeviceClass);
}

// --- PORT LOGIC (Gemini Lake Safe - NO CLEAR) ---

void XhciDriver::reset_port(int port) {
    printf("XHCI: Resetting port %d...\n", port);
    
    uint32_t sc = read_port_reg(port, 0);
    
    // 1. Assert Reset (Strict, No Clear)
    uint32_t reset_val = sc;
    reset_val &= ~PORT_CHANGE_MASK;  
    reset_val |= PORT_RESET;         
    reset_val |= PORT_POWER;         
    
    write_port_reg(port, 0, reset_val);
    
    // 2. Wait for reset complete
    int timeout = 150;
    bool reset_completed = false;
    
    while (timeout--) {
        sleep_ms(1);
        sc = read_port_reg(port, 0);
        
        if (!(sc & PORT_POWER)) {
            // If power lost, restore it and abort enumeration
            write_port_reg(port, 0, PORT_POWER); 
            return; 
        }
        
        if (!(sc & PORT_RESET)) {
            reset_completed = true;
            break;
        }
    }
    
    if (!reset_completed) {
        printf("XHCI: Port %d reset timeout.\n", port);
        return;
    }
    
    sleep_ms(50); // Stabilize
    
    // 3. Check Enable Status
    sc = read_port_reg(port, 0);
    if (sc & PORT_PE) {
        printf("XHCI: Port %d Enabled. Enumerating...\n", port);
        enumerate_device(port);
    } else {
        printf("XHCI: Port %d Reset done but NOT enabled. (Status: 0x%x)\n", port, sc);
    }
    
    // NOTE: We do NOT clear change bits. Leaving them 1 is safer on this hardware.
}

void XhciDriver::xhci_port_manager() {
    printf("XHCI: Port Manager\n");
    
    uint32_t status = read_op_reg(XHCI_USBSTS);
    if (status & 0x1) {
        printf("XHCI: ERROR - Controller halted!\n");
        return;
    }
    
    // PHASE 1: Power on
    bool any_powered = false;
    for (uint32_t i = 1; i <= max_ports; i++) {
        uint32_t sc = read_port_reg(i, 0);
        if (!(sc & PORT_POWER)) {
            write_port_reg(i, 0, (sc & ~PORT_CHANGE_MASK) | PORT_POWER);
            any_powered = true;
        }
    }
    
    if (any_powered) sleep_ms(100);

    // PHASE 2: Reset connected
    for (uint32_t i = 1; i <= max_ports; i++) {
        uint32_t sc = read_port_reg(i, 0);
        if (!(sc & PORT_POWER)) continue; 
        
        // Only reset if connected
        if ((sc & PORT_CONNECT) && !(sc & PORT_PE)) {
            reset_port(i);
        }
    }
    
    printf("XHCI: Ports Scanned.\n");
}

void XhciDriver::poll_ports() {
    for (uint32_t i = 1; i <= max_ports; i++) {
        uint32_t sc = read_port_reg(i, 0);
        uint32_t changes = sc & PORT_CHANGE_MASK;
        
        if (changes) {
            // Do not clear bits.
            if ((changes & PORT_CSC) && (sc & PORT_CONNECT)) {
                if (!(sc & PORT_PE)) {
                    printf("XHCI: Hotplug Port %d\n", i);
                    reset_port(i);
                }
            }
        }
    }
}

// ---------------------------------------------

bool XhciDriver::init(uint16_t vendor_id, uint16_t device_id) {
    printf("XHCI: Initializing for Gemini Lake J4125\n");
    
    bool found = false;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            if (pci_read_word(bus, slot, 0, 0) == vendor_id && 
                pci_read_word(bus, slot, 0, 2) == device_id) {
                pci_dev.bus = bus; 
                pci_dev.slot = slot; 
                pci_dev.function = 0;
                pci_dev.vendor_id = vendor_id; 
                pci_dev.device_id = device_id;
                uint32_t b0 = pci_read_dword(bus, slot, 0, 0x10);
                uint32_t b1 = pci_read_dword(bus, slot, 0, 0x14);
                pci_dev.bar0 = (b0 & 0xFFFFFFF0) | ((uint64_t)b1 << 32);
                pci_dev.irq_line = pci_read_byte(bus, slot, 0, 0x3C);
                printf("XHCI: Found %02x:%02x (IRQ %d)\n", bus, slot, pci_dev.irq_line);
                found = true; 
                goto dev_found;
            }
        }
    }
dev_found:
    if (!found) return false;

    pci_enable_bus_mastering(&pci_dev);
    
    // Unmask Legacy IRQ if valid (though we likely use MSI/Poll)
    if (pci_dev.irq_line > 0 && pci_dev.irq_line < 16) {
        pic_unmask(pci_dev.irq_line);
    }

    mmio_base_phys = pci_dev.bar0;
    mmio_base_virt = XHCI_VIRT_BASE;
    for (int i = 0; i < 32; i++) {
        vmm_map_page(mmio_base_virt + i*4096, mmio_base_phys + i*4096, 3);
    }

    perform_bios_handoff();

    cap_length = *(volatile uint8_t*)(mmio_base_virt + XHCI_CAPLENGTH);
    op_regs_base = mmio_base_virt + cap_length;
    uint32_t hcs1 = *(volatile uint32_t*)(mmio_base_virt + XHCI_HCSPARAMS1);
    max_slots = hcs1 & 0xFF;
    max_ports = (hcs1 >> 24) & 0xFF;
    uint32_t dboff = *(volatile uint32_t*)(mmio_base_virt + XHCI_DBOFF);
    uint32_t rtsoff = *(volatile uint32_t*)(mmio_base_virt + XHCI_RTSOFF);
    run_regs_base = mmio_base_virt + (rtsoff & ~0x1F);
    db_regs_base = mmio_base_virt + (dboff & ~0x3);
    
    uint32_t hcc1 = *(volatile uint32_t*)(mmio_base_virt + XHCI_HCCPARAMS1);
    context_size_bytes = (hcc1 & (1 << 2)) ? 64 : 32;
    printf("XHCI: Context Size = %d\n", context_size_bytes);

    if (!reset_controller()) return false;

    init_memory();

    uint32_t cmd = read_op_reg(XHCI_USBCMD);
    cmd |= USBCMD_RUN;
    cmd |= USBCMD_INTE;
    write_op_reg(XHCI_USBCMD, cmd);
    
    int timeout = 100;
    while (timeout--) {
        if (!(read_op_reg(XHCI_USBSTS) & 0x1)) break; 
        sleep_ms(1);
    }
    sleep_ms(50);

    xhci_port_manager();
    
    return true;
}

bool XhciDriver::reset_controller() {
    printf("XHCI: Resetting controller...\n");
    uint32_t cmd = read_op_reg(XHCI_USBCMD);
    if (cmd & USBCMD_RUN) {
        write_op_reg(XHCI_USBCMD, cmd & ~USBCMD_RUN);
        sleep_ms(20);
    }
    write_op_reg(XHCI_USBCMD, USBCMD_RESET);
    sleep_ms(1);
    int t = 1000;
    while ((read_op_reg(XHCI_USBCMD) & USBCMD_RESET) && t--) sleep_ms(1);
    return t > 0;
}

void XhciDriver::init_memory() {
    printf("XHCI: Init Memory\n");
    uint32_t config = read_op_reg(XHCI_CONFIG);
    config &= ~0xFF; 
    config |= max_slots;
    write_op_reg(XHCI_CONFIG, config);

    dcbaa = (uint64_t*)alloc_aligned((max_slots+1)*8, 64);
    memset(dcbaa, 0, (max_slots+1)*8);
    write_op_reg64(XHCI_DCBAAP, get_phys(dcbaa));

    cmd_ring_base = alloc_aligned(4096, 64);
    memset(cmd_ring_base, 0, 4096);
    cmd_ring_enqueue_ptr = 0;
    cmd_ring_cycle = 1;
    write_op_reg64(XHCI_CRCR, get_phys(cmd_ring_base) | 1);

    erst = (uint64_t*)alloc_aligned(16, 64);
    event_ring_base = alloc_aligned(4096, 64);
    memset(event_ring_base, 0, 4096);
    
    event_ring_segment = event_ring_base;
    event_ring_dequeue_ptr = event_ring_base;
    event_ring_cycle = 1;

    erst[0] = get_phys(event_ring_base);
    erst[1] = (4096/16);

    *(volatile uint32_t*)(run_regs_base + XHCI_ERSTSZ(0)) = 1;
    uint64_t ep = get_phys(erst);
    *(volatile uint32_t*)(run_regs_base + XHCI_ERSTBA(0)) = (uint32_t)ep;
    *(volatile uint32_t*)(run_regs_base + XHCI_ERSTBA(0)+4) = (uint32_t)(ep >> 32);
    
    uint64_t erdp = get_phys(event_ring_base);
    *(volatile uint32_t*)(run_regs_base + XHCI_ERDP(0)) = (uint32_t)erdp | 8;
    *(volatile uint32_t*)(run_regs_base + XHCI_ERDP(0)+4) = (uint32_t)(erdp >> 32);
    
    uint32_t iman = *(volatile uint32_t*)(run_regs_base + XHCI_IMAN(0));
    iman |= 2; 
    iman |= 1; 
    *(volatile uint32_t*)(run_regs_base + XHCI_IMAN(0)) = iman;
    
    *(volatile uint32_t*)(run_regs_base + XHCI_IMOD(0)) = 0;
}

void XhciDriver::poll_events() {
    // CRITICAL SAFETY CHECK: If XHCI hasn't been initialized, 
    // the pointer will be null. Accessing it causes Page Fault 0xE at 0xC.
    if (!event_ring_dequeue_ptr) return;

    XhciEventTrb* trb = (XhciEventTrb*)event_ring_dequeue_ptr;
    while ((trb->control & 1) == event_ring_cycle) {
        
        uint64_t type = (trb->control >> 10) & 0x3F;
        
        if (type == 34) { // Port Status Change
            poll_ports();
        }

        uint64_t next = (uint64_t)event_ring_dequeue_ptr + 16;
        if (next >= (uint64_t)event_ring_segment + 4096) {
            next = (uint64_t)event_ring_segment;
            event_ring_cycle ^= 1; 
        }
        event_ring_dequeue_ptr = (void*)next;
        trb = (XhciEventTrb*)event_ring_dequeue_ptr;
    }
    
    uint64_t erdp_val = get_phys(event_ring_dequeue_ptr);
    *(volatile uint32_t*)(run_regs_base + XHCI_ERDP(0)) = (uint32_t)erdp_val | 8; 
    *(volatile uint32_t*)(run_regs_base + XHCI_ERDP(0) + 4) = (uint32_t)(erdp_val >> 32);
    
    uint32_t iman = *(volatile uint32_t*)(run_regs_base + XHCI_IMAN(0));
    if (iman & 1) {
        *(volatile uint32_t*)(run_regs_base + XHCI_IMAN(0)) = iman | 1; 
    }
}