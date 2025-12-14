#include "ahci.h"
#include "../../memory/pmm.h"
#include "../../memory/vmm.h"
#include "../../cppstd/stdio.h"
#include "../../cppstd/string.h"
#include "../../timer.h"

static void* phys_to_virt(uint64_t phys) {
    return (void*)(phys + g_hhdm_offset);
}

static uint64_t virt_to_phys_addr(void* vaddr) {
    uint64_t addr = (uint64_t)vaddr;
    if (addr >= g_hhdm_offset) {
        return addr - g_hhdm_offset;
    }
    return vmm_virt_to_phys(addr);
}

AhciDriver& AhciDriver::getInstance() {
    static AhciDriver instance;
    return instance;
}

AhciDriver::AhciDriver() : hba_mem(nullptr) {
    memset(ports, 0, sizeof(ports));
}

bool AhciDriver::init() {
    printf("AHCI: Scanning for Controller...\n");
    if (!pci_find_device_by_class(0x01, 0x06, &pci_dev)) {
        printf("AHCI: No SATA Controller found.\n");
        return false;
    }

    uint32_t bar5 = pci_read_dword(pci_dev.bus, pci_dev.slot, pci_dev.function, 0x24);
    printf("AHCI: Found Controller at %02x:%02x (BAR5: 0x%x)\n", 
           pci_dev.bus, pci_dev.slot, bar5);

    if (bar5 == 0) {
        printf("AHCI: Critical Error - BAR5 is 0.\n");
        return false;
    }

    uint32_t cmd = pci_read_dword(pci_dev.bus, pci_dev.slot, pci_dev.function, 0x04);
    cmd |= (1 << 1) | (1 << 2); 
    cmd &= ~(1 << 10); 
    pci_write_dword(pci_dev.bus, pci_dev.slot, pci_dev.function, 0x04, cmd);

    mmio_base_phys = bar5 & 0xFFFFFFF0;
    mmio_base_virt = 0xFFFFA00010000000; 

    vmm_map_page(mmio_base_virt, mmio_base_phys, PTE_PRESENT | PTE_RW | PTE_PCD);
    vmm_map_page(mmio_base_virt + 4096, mmio_base_phys + 4096, PTE_PRESENT | PTE_RW | PTE_PCD);

    hba_mem = (HBA_MEM*)mmio_base_virt;
    printf("AHCI: Hardware Version %d.%d\n", 
        (hba_mem->vs >> 16) & 0xFFFF, hba_mem->vs & 0xFFFF);

    hba_mem->ghc |= (1 << 31);
    
    probe_ports();
    return true;
}

void AhciDriver::probe_ports() {
    uint32_t pi = hba_mem->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & 1) {
            int dt = check_type(&hba_mem->ports[i]);
            if (dt == AHCI_DEV_SATA) {
                printf("AHCI: Port %d: SATA Device Found\n", i);
                ports[i].implemented = true;
                ports[i].type = dt;
                ports[i].port_reg = &hba_mem->ports[i];
                ports[i].id = i;
                rebase_port(&ports[i]);
            } else if (dt == AHCI_DEV_SATAPI) {
                printf("AHCI: Port %d: SATAPI (CD-ROM)\n", i);
            }
        }
        pi >>= 1;
    }
}

int AhciDriver::findFirstSataPort() {
    for (int i = 0; i < 32; i++) {
        if (ports[i].implemented && ports[i].type == AHCI_DEV_SATA) return i;
    }
    return -1;
}

int AhciDriver::check_type(HBA_PORT* port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE)
        return AHCI_DEV_NULL;

    switch (port->sig) {
        case 0xEB140101: return AHCI_DEV_SATAPI;
        case 0xC33C0101: return AHCI_DEV_SEMB;
        case 0x96690101: return AHCI_DEV_PM;
        default: return AHCI_DEV_SATA;
    }
}

void AhciDriver::stop_cmd(HBA_PORT* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;
    
    int timeout = 1000;
    while(true) {
        if (timeout-- <= 0) break;
        if (port->cmd & HBA_PxCMD_FR) { sleep_ms(1); continue; }
        if (port->cmd & HBA_PxCMD_CR) { sleep_ms(1); continue; }
        break;
    }
}

void AhciDriver::start_cmd(HBA_PORT* port) {
    while (port->cmd & HBA_PxCMD_CR) asm volatile("pause"); 
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

void AhciDriver::rebase_port(AhciPort* p) {
    stop_cmd(p->port_reg);

    void* cmd_list_phys_addr = pmm_alloc(1);
    p->cmd_list_phys = (uint64_t)cmd_list_phys_addr;
    p->cmd_list = (HBA_CMD_HEADER*)phys_to_virt(p->cmd_list_phys);
    memset(p->cmd_list, 0, 4096);

    p->port_reg->clb = (uint32_t)(p->cmd_list_phys & 0xFFFFFFFF);
    p->port_reg->clbu = (uint32_t)(p->cmd_list_phys >> 32);

    p->fis_base_phys = p->cmd_list_phys + 1024;
    p->fis_base = (void*)((uint64_t)p->cmd_list + 1024);
    
    p->port_reg->fb = (uint32_t)(p->fis_base_phys & 0xFFFFFFFF);
    p->port_reg->fbu = (uint32_t)(p->fis_base_phys >> 32);

    void* cmd_table_phys_addr = pmm_alloc(1);
    p->cmd_table_phys = (uint64_t)cmd_table_phys_addr;
    p->cmd_table = (HBA_CMD_TABLE*)phys_to_virt(p->cmd_table_phys);
    memset(p->cmd_table, 0, 4096);

    HBA_CMD_HEADER* hdr = &p->cmd_list[0];
    hdr->ctba = (uint32_t)(p->cmd_table_phys & 0xFFFFFFFF);
    hdr->ctbau = (uint32_t)(p->cmd_table_phys >> 32);
    hdr->prdtl = 1; 

    start_cmd(p->port_reg);
}

int AhciDriver::find_cmd_slot(HBA_PORT* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i=0; i<32; i++) {
        if ((slots & 1) == 0) return i;
        slots >>= 1;
    }
    return -1;
}

// --- IO OPS WITH TIMEOUTS ---

bool AhciDriver::read(int port_index, uint64_t lba, uint32_t count, void* buffer) {
    if (port_index < 0 || port_index >= 32) return false;
    if (!ports[port_index].implemented) return false;

    AhciPort* p = &ports[port_index];
    HBA_PORT* reg = p->port_reg;

    reg->is = (uint32_t)-1;
    int spin = 0;
    int slot = find_cmd_slot(reg);
    if (slot == -1) return false;

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)p->cmd_list;
    cmdheader += slot;
    cmdheader->cfl = sizeof(FIS_REG_H2D)/sizeof(uint32_t); 
    cmdheader->w = 0; 
    cmdheader->prdtl = 1;

    HBA_CMD_TABLE* cmdtable = (HBA_CMD_TABLE*)p->cmd_table;
    memset(cmdtable, 0, sizeof(HBA_CMD_TABLE) + (cmdheader->prdtl-1)*sizeof(HBA_PRDT_ENTRY));

    uint64_t buf_phys = virt_to_phys_addr(buffer);
    
    cmdtable->prdt_entry[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmdtable->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtable->prdt_entry[0].dbc = (count * 512) - 1;
    cmdtable->prdt_entry[0].i = 1;

    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtable->cfis);
    cmdfis->fis_type = 0x27;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_READ_DMA_EX;

    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->device = 1 << 6; 
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    while ((reg->tfd & (0x80 | 0x08)) && spin < 1000000) { spin++; }
    if (spin == 1000000) { printf("AHCI: BUSY Timeout\n"); return false; }

    reg->ci |= (1 << slot);

    // Timeout loop ~2 seconds
    uint64_t timeout = 2000; 
    while (true) {
        if ((reg->ci & (1 << slot)) == 0) break;
        if (reg->is & (1 << 30)) { printf("AHCI: Disk Error\n"); return false; }
        
        sleep_ms(1);
        if (timeout-- == 0) {
            printf("AHCI: Timeout waiting for Read completion.\n");
            return false;
        }
    }
    return true;
}

bool AhciDriver::write(int port_index, uint64_t lba, uint32_t count, const void* buffer) {
    if (port_index < 0 || port_index >= 32) return false;
    if (!ports[port_index].implemented) return false;

    AhciPort* p = &ports[port_index];
    HBA_PORT* reg = p->port_reg;

    reg->is = (uint32_t)-1;
    int spin = 0;
    int slot = find_cmd_slot(reg);
    if (slot == -1) return false;

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)p->cmd_list;
    cmdheader += slot;
    cmdheader->cfl = sizeof(FIS_REG_H2D)/sizeof(uint32_t); 
    cmdheader->w = 1; 
    cmdheader->prdtl = 1;

    HBA_CMD_TABLE* cmdtable = (HBA_CMD_TABLE*)p->cmd_table;
    memset(cmdtable, 0, sizeof(HBA_CMD_TABLE) + (cmdheader->prdtl-1)*sizeof(HBA_PRDT_ENTRY));

    uint64_t buf_phys = virt_to_phys_addr((void*)buffer);
    
    cmdtable->prdt_entry[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmdtable->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtable->prdt_entry[0].dbc = (count * 512) - 1;
    cmdtable->prdt_entry[0].i = 1;

    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtable->cfis);
    cmdfis->fis_type = 0x27;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_WRITE_DMA_EX;

    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->device = 1 << 6;
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    while ((reg->tfd & (0x80 | 0x08)) && spin < 1000000) { spin++; }
    if (spin == 1000000) return false;

    reg->ci |= (1 << slot);

    uint64_t timeout = 2000;
    while (true) {
        if ((reg->ci & (1 << slot)) == 0) break;
        if (reg->is & (1 << 30)) { printf("AHCI: Disk Error\n"); return false; }
        
        sleep_ms(1);
        if (timeout-- == 0) {
            printf("AHCI: Timeout waiting for Write completion.\n");
            return false;
        }
    }
    return true;
}