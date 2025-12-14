#include "e1000.h"
#include "../../memory/vmm.h"
#include "../../memory/pmm.h"
#include "../../memory/heap.h"
#include "../../cppstd/stdio.h"
#include "../../cppstd/string.h"
#include "../../timer.h"
#include "../../interrupts/pic.h"
#include "../../net/network.h" 

#define E1000_MMIO_VIRT 0xFFFFA00030000000

E1000Driver::E1000Driver() : mmio_base_virt(0), initialized(false), rx_cur(0), tx_cur(0) {
    memset(mac_addr, 0, 6);
    pci_dev.irq_line = 0xFF; 
}

E1000Driver& E1000Driver::getInstance() {
    static E1000Driver instance;
    return instance;
}

void E1000Driver::write_reg(uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)(mmio_base_virt + offset) = val;
}

uint32_t E1000Driver::read_reg(uint32_t offset) {
    return *(volatile uint32_t*)(mmio_base_virt + offset);
}

void E1000Driver::detect_eeprom() {
    write_reg(E1000_EERD, 0x1);
    int i = 0;
    has_eeprom = false;
    for (i = 0; i < 1000; i++) {
        if (read_reg(E1000_EERD) & 0x10) {
            has_eeprom = true;
            break;
        }
    }
}

uint16_t E1000Driver::read_eeprom(uint32_t addr) {
    uint32_t tmp = 0;
    if (has_eeprom) {
        write_reg(E1000_EERD, 1 | (addr << 8));
        while (!((tmp = read_reg(E1000_EERD)) & (1 << 4)));
    } else {
        write_reg(E1000_EERD, 1 | (addr << 2));
        while (!((tmp = read_reg(E1000_EERD)) & (1 << 1)));
    }
    return (uint16_t)((tmp >> 16) & 0xFFFF);
}

void E1000Driver::read_mac() {
    if (has_eeprom) {
        uint16_t temp;
        temp = read_eeprom(0);
        mac_addr[0] = temp & 0xff;
        mac_addr[1] = temp >> 8;
        temp = read_eeprom(1);
        mac_addr[2] = temp & 0xff;
        mac_addr[3] = temp >> 8;
        temp = read_eeprom(2);
        mac_addr[4] = temp & 0xff;
        mac_addr[5] = temp >> 8;
    } else {
        uint32_t mac_low = read_reg(E1000_RAL);
        uint32_t mac_high = read_reg(E1000_RAH);
        mac_addr[0] = mac_low & 0xFF;
        mac_addr[1] = (mac_low >> 8) & 0xFF;
        mac_addr[2] = (mac_low >> 16) & 0xFF;
        mac_addr[3] = (mac_low >> 24) & 0xFF;
        mac_addr[4] = mac_high & 0xFF;
        mac_addr[5] = (mac_high >> 8) & 0xFF;
    }
    printf("E1000: MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

void E1000Driver::shutdown() {
    if (!initialized) return;
    printf("E1000: Shutting down device...\n");
    
    // Mask all interrupts
    write_reg(E1000_IMC, 0xFFFFFFFF);
    
    // Disable RX and TX
    write_reg(E1000_RCTL, 0);
    write_reg(E1000_TCTL, 0);
    
    initialized = false;
    // Note: We leak the PMM pages for rings here for simplicity, 
    // real OS would store them and reuse or free them.
}

bool E1000Driver::init() {
    if (initialized) return true;

    printf("E1000: Scanning for Intel 82540EM...\n");
    
    if (!pci_find_device_by_class(0x02, 0x00, &pci_dev)) {
        printf("E1000: No Ethernet Controller found.\n");
        return false;
    }
    
    if (pci_dev.vendor_id != 0x8086) {
        printf("E1000: Found NIC but not Intel.\n");
        return false;
    }

    printf("E1000: Found Device %04x at BAR0 %x, IRQ %d\n", 
        pci_dev.device_id, pci_dev.bar0, pci_dev.irq_line);

    pci_enable_bus_mastering(&pci_dev);
    
    mmio_base_phys = pci_dev.bar0;
    mmio_base_virt = E1000_MMIO_VIRT;
    
    for(int i=0; i<32; i++) {
        vmm_map_page(mmio_base_virt + i*4096, mmio_base_phys + i*4096, 3);
    }

    detect_eeprom();
    read_mac();

    write_reg(E1000_IMC, 0xFFFFFFFF);
    write_reg(E1000_CTRL, E1000_CTRL_SLU); 

    // Setup RX
    void* rx_ring_phys = pmm_alloc(1);
    void* rx_ring_virt = (void*)((uint64_t)rx_ring_phys + 0xFFFF800000000000); 
    vmm_map_page((uint64_t)rx_ring_virt, (uint64_t)rx_ring_phys, 3);
    
    rx_descs = (e1000_rx_desc*)rx_ring_virt;
    memset(rx_descs, 0, 4096);

    for(int i=0; i<E1000_NUM_RX_DESC; i++) {
        void* buf_phys = pmm_alloc(1);
        uint64_t buf_virt_addr = (uint64_t)buf_phys + 0xFFFF800000000000;
        vmm_map_page(buf_virt_addr, (uint64_t)buf_phys, 3);
        
        rx_buffers[i] = (uint8_t*)buf_virt_addr;
        
        rx_descs[i].addr = (uint64_t)buf_phys;
        rx_descs[i].status = 0;
    }

    write_reg(E1000_RDBAL, (uint32_t)((uint64_t)rx_ring_phys & 0xFFFFFFFF));
    write_reg(E1000_RDBAH, (uint32_t)((uint64_t)rx_ring_phys >> 32));
    write_reg(E1000_RDLEN, E1000_NUM_RX_DESC * 16);
    write_reg(E1000_RDH, 0);
    write_reg(E1000_RDT, E1000_NUM_RX_DESC - 1);
    
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_LPE | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    write_reg(E1000_RCTL, rctl);

    // Setup TX
    void* tx_ring_phys = pmm_alloc(1);
    void* tx_ring_virt = (void*)((uint64_t)tx_ring_phys + 0xFFFF800000000000);
    vmm_map_page((uint64_t)tx_ring_virt, (uint64_t)tx_ring_phys, 3);
    
    tx_descs = (e1000_tx_desc*)tx_ring_virt;
    memset(tx_descs, 0, 4096);

    for(int i=0; i<E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr = 0;
        tx_descs[i].cmd = 0;
        tx_descs[i].status = 1; 
    }

    write_reg(E1000_TDBAL, (uint32_t)((uint64_t)tx_ring_phys & 0xFFFFFFFF));
    write_reg(E1000_TDBAH, (uint32_t)((uint64_t)tx_ring_phys >> 32));
    write_reg(E1000_TDLEN, E1000_NUM_TX_DESC * 16);
    write_reg(E1000_TDH, 0);
    write_reg(E1000_TDT, 0);

    write_reg(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x0F << 4) | (0x40 << 12));

    // Enable Interrupts
    write_reg(E1000_IMS, E1000_ICR_LSC | E1000_ICR_RXT0);
    
    if (pci_dev.irq_line > 0 && pci_dev.irq_line < 16) {
        printf("E1000: Unmasking IRQ %d\n", pci_dev.irq_line);
        pic_unmask(pci_dev.irq_line);
    }

    NetworkStack::getInstance().init();

    printf("E1000: Initialized.\n");
    initialized = true;
    return true;
}

void E1000Driver::send_packet(const uint8_t* data, uint16_t len) {
    if (!initialized) return;
    tx_cur = read_reg(E1000_TDT);
    
    void* page = pmm_alloc(1);
    void* virt = (void*)((uint64_t)page + 0xFFFF800000000000);
    vmm_map_page((uint64_t)virt, (uint64_t)page, 3);
    
    memcpy(virt, data, len);
    
    tx_descs[tx_cur].addr = (uint64_t)page;
    tx_descs[tx_cur].length = len;
    tx_descs[tx_cur].cmd = E1000_CMD_EOP | E1000_CMD_IFCS | E1000_CMD_RS; 
    tx_descs[tx_cur].status = 0;
    
    uint16_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    write_reg(E1000_TDT, tx_cur);
    
    int timeout = 100000;
    while (!(tx_descs[old_cur].status & 0xff) && timeout-- > 0) {
        asm("pause");
    }
    
    pmm_free(page, 1);
}

void E1000Driver::handle_interrupt() {
    if (!initialized) return;
    uint32_t icr = read_reg(E1000_ICR);
    
    if (icr & E1000_ICR_LSC) {
        printf("E1000: Link Status Change\n");
    }
    
    if (icr & E1000_ICR_RXT0) {
        while ((rx_descs[rx_cur].status & 1)) {
            uint8_t* buf = rx_buffers[rx_cur];
            uint16_t len = rx_descs[rx_cur].length;
            
            NetworkStack::getInstance().handle_packet(buf, len);
                
            rx_descs[rx_cur].status = 0;
            uint16_t old_cur = rx_cur;
            rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
            write_reg(E1000_RDT, old_cur);
        }
    }
}