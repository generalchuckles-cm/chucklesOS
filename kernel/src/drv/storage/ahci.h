#ifndef AHCI_H
#define AHCI_H

#include <cstdint>
#include <cstddef>
#include "ahci_defs.h"
#include "../../pci/pci.h"

struct AhciPort {
    int id;
    HBA_PORT* port_reg;
    HBA_CMD_HEADER* cmd_list; // Virtual Address
    uint64_t cmd_list_phys;
    HBA_CMD_TABLE* cmd_table; // Virtual Address
    uint64_t cmd_table_phys;
    void* fis_base;           // Virtual Address
    uint64_t fis_base_phys;
    bool implemented;
    int type; // SATA, SATAPI, etc.
};

class AhciDriver {
public:
    static AhciDriver& getInstance();
    
    // Finds PCI device and initializes HBA
    bool init();
    
    // Returns the index of the first connected SATA drive, or -1 if none.
    int findFirstSataPort();

    // Read sectors (512 bytes each)
    bool read(int port_index, uint64_t lba, uint32_t count, void* buffer);

    // Write sectors
    bool write(int port_index, uint64_t lba, uint32_t count, const void* buffer);

private:
    AhciDriver();
    
    PCIDevice pci_dev;
    uint64_t mmio_base_phys;
    uint64_t mmio_base_virt;
    HBA_MEM* hba_mem;

    AhciPort ports[32];

    void probe_ports();
    int  check_type(HBA_PORT* port);
    void start_cmd(HBA_PORT* port);
    void stop_cmd(HBA_PORT* port);
    void rebase_port(AhciPort* port);
    
    int find_cmd_slot(HBA_PORT* port);
};

#endif