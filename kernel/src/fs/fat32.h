#ifndef FAT32_H
#define FAT32_H

#include "fat32_defs.h"
#include "../drv/storage/ahci.h"

class Fat32 {
public:
    static Fat32& getInstance();

    // Init: Reads sector 0, parses BPB, calculates offsets
    bool init(int port);

    // Format: Wipes disk, writes new BPB/FSInfo/FATs
    bool format(int port, uint32_t size_sectors);

    // List files in current directory (Root)
    void ls();

    // File Operations
    bool read_file(const char* filename, void* buffer, uint32_t buffer_len);
    bool create_file(const char* filename); // Creates empty file
    bool write_file(const char* filename, void* data, uint32_t len);

private:
    Fat32();
    
    int port_index;
    bool mounted;
    Fat32BootSector bpb;

    // Calculated offsets (in sectors)
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;

    // Helpers
    uint32_t cluster_to_lba(uint32_t cluster);
    uint32_t get_next_cluster(uint32_t cluster);
    void     set_next_cluster(uint32_t cluster, uint32_t next);
    uint32_t allocate_cluster();

    // String Helpers
    void to_dos_filename(const char* input, char* dest_name, char* dest_ext);
    bool compare_filename(FatDirectoryEntry* entry, const char* name, const char* ext);

    // Directory Helpers
    uint32_t find_entry(const char* filename, FatDirectoryEntry* out_entry, uint32_t* out_dir_cluster, uint32_t* out_dir_offset);
    bool update_entry(uint32_t dir_cluster, uint32_t offset, FatDirectoryEntry* entry);
};

#endif