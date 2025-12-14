#include "fat32.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"

// --- DMA Allocator Helpers ---
static void* dma_alloc(size_t pages) {
    void* phys = pmm_alloc(pages);
    if (!phys) return nullptr;
    return (void*)((uint64_t)phys + g_hhdm_offset);
}

static void dma_free(void* virt, size_t pages) {
    if (!virt) return;
    uint64_t phys = (uint64_t)virt - g_hhdm_offset;
    pmm_free((void*)phys, pages);
}

Fat32& Fat32::getInstance() {
    static Fat32 instance;
    return instance;
}

Fat32::Fat32() : mounted(false) {
    memset(&bpb, 0, sizeof(Fat32BootSector));
}

uint32_t Fat32::cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return data_start_lba + ((cluster - 2) * bpb.sectors_per_cluster);
}

// --- HELPER: FAT Table Lookup ---
uint32_t Fat32::get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) return 0;

    if (!AhciDriver::getInstance().read(port_index, fat_sector, 1, buf)) {
        dma_free(buf, 1);
        return 0;
    }
    uint32_t val = *(uint32_t*)&buf[ent_offset];
    dma_free(buf, 1);

    return val & 0x0FFFFFFF; 
}

void Fat32::set_next_cluster(uint32_t cluster, uint32_t next) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) return;

    AhciDriver::getInstance().read(port_index, fat_sector, 1, buf);
    *(uint32_t*)&buf[ent_offset] = (next & 0x0FFFFFFF);
    
    // Write to all FAT copies
    for (int i = 0; i < bpb.fat_count; i++) {
        AhciDriver::getInstance().write(port_index, fat_sector + (i * sectors_per_fat), 1, buf);
    }
    dma_free(buf, 1);
}

// --- HELPER: Find Free Cluster ---
uint32_t Fat32::allocate_cluster() {
    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) return 0;

    for (uint32_t i = 0; i < sectors_per_fat; i++) {
        AhciDriver::getInstance().read(port_index, fat_start_lba + i, 1, buf);
        uint32_t* table = (uint32_t*)buf;
        
        for (int j = 0; j < 128; j++) {
             if (i == 0 && j < 2) continue; // Reserved entries

             if ((table[j] & 0x0FFFFFFF) == FAT32_ENTRY_FREE) {
                 uint32_t cluster = (i * 128) + j;
                 
                 // Mark as End of Chain
                 table[j] = FAT32_ENTRY_EOC;
                 
                 // Write updated FAT
                 for (int f = 0; f < bpb.fat_count; f++) {
                     AhciDriver::getInstance().write(port_index, fat_start_lba + i + (f * sectors_per_fat), 1, buf);
                 }
                 
                 // Zero out the actual data cluster
                 uint8_t* zero = (uint8_t*)dma_alloc(1); 
                 if (zero) {
                     memset(zero, 0, 4096);
                     // Note: Assumes 4KB cluster (8 sectors). 
                     AhciDriver::getInstance().write(port_index, cluster_to_lba(cluster), bpb.sectors_per_cluster, zero);
                     dma_free(zero, 1);
                 }
                 
                 dma_free(buf, 1);
                 return cluster;
             }
        }
    }
    dma_free(buf, 1);
    return 0;
}

void Fat32::to_dos_filename(const char* input, char* dest_name, char* dest_ext) {
    memset(dest_name, ' ', 8);
    memset(dest_ext, ' ', 3);
    
    int i = 0, j = 0;
    while(input[i] && input[i] != '.' && j < 8) {
        char c = input[i++];
        if(c >= 'a' && c <= 'z') c -= 32;
        dest_name[j++] = c;
    }
    while(input[i] && input[i] != '.') i++;
    if(input[i] == '.') {
        i++;
        j = 0;
        while(input[i] && j < 3) {
            char c = input[i++];
            if(c >= 'a' && c <= 'z') c -= 32;
            dest_ext[j++] = c;
        }
    }
}

// --- INIT ---
bool Fat32::init(int port) {
    port_index = port;
    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) { printf("FAT32: OOM\n"); return false; }
    
    // Read BPB (Sector 0)
    if (!AhciDriver::getInstance().read(port, 0, 1, buf)) {
        printf("FAT32: Read Error on Port %d (Is AHCI Init?)\n", port);
        dma_free(buf, 1); 
        return false;
    }

    memcpy(&bpb, buf, sizeof(Fat32BootSector));
    dma_free(buf, 1);

    // Validation: 0x29 sig AND 512 byte sectors
    if (bpb.boot_signature != 0x29 || bpb.bytes_per_sector != 512) {
        // Corrected format string to avoid double 0x0x
        printf("FAT32: Invalid Sig (%x) or Sector Size (%d)\n", bpb.boot_signature, bpb.bytes_per_sector);
        return false;
    }

    sectors_per_fat = bpb.sectors_per_fat_32;
    fat_start_lba = bpb.reserved_sectors;
    data_start_lba = fat_start_lba + (bpb.fat_count * sectors_per_fat);
    root_cluster = bpb.root_cluster;

    printf("FAT32: Mounted Port %d (Root @ %d)\n", port, root_cluster);
    mounted = true;
    return true;
}

// --- FORMAT ---
bool Fat32::format(int port, uint32_t size_sectors) {
    printf("FAT32: Formatting Port %d (%d sectors)...\n", port, size_sectors);
    
    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) return false;
    memset(buf, 0, 4096);

    Fat32BootSector* new_bpb = (Fat32BootSector*)buf;
    new_bpb->jmp_boot[0] = 0xEB; new_bpb->jmp_boot[1] = 0x3C; new_bpb->jmp_boot[2] = 0x90;
    memcpy(new_bpb->oem_name, "MSWIN4.1", 8);
    new_bpb->bytes_per_sector = 512;
    new_bpb->sectors_per_cluster = 8; // 4KB
    new_bpb->reserved_sectors = 32;
    new_bpb->fat_count = 2;
    new_bpb->media_type = 0xF8;
    new_bpb->hidden_sectors = 0;
    new_bpb->total_sectors_32 = size_sectors;
    
    uint32_t total_clusters = size_sectors / 8;
    new_bpb->sectors_per_fat_32 = (total_clusters * 4 + 511) / 512;
    new_bpb->root_cluster = 2;
    new_bpb->fs_info_sector = 1;
    new_bpb->backup_boot_sector = 6;
    new_bpb->boot_signature = 0x29;
    memcpy(new_bpb->fs_type, "FAT32   ", 8);
    new_bpb->volume_id = 0xCAFEBABE;

    // === CRITICAL FIX: Save values before buffer reuse ===
    uint32_t saved_reserved = new_bpb->reserved_sectors;
    uint32_t saved_sectors_fat = new_bpb->sectors_per_fat_32;
    uint32_t saved_fat_count = new_bpb->fat_count;
    // ===================================================

    // 1. Write BPB
    if (!AhciDriver::getInstance().write(port, 0, 1, buf)) {
        printf("FAT32: Write BPB Failed.\n");
        dma_free(buf, 1); return false;
    }

    // 2. Write FSInfo (Reuses buf, zeroing previous data)
    memset(buf, 0, 512);
    FSInfo* info = (FSInfo*)buf;
    info->lead_sig = 0x41615252;
    info->struct_sig = 0x61417272;
    info->trail_sig = 0xAA550000;
    info->free_count = 0xFFFFFFFF;
    info->next_free = 0xFFFFFFFF;
    AhciDriver::getInstance().write(port, 1, 1, buf);

    dma_free(buf, 1);

    // 3. ZERO OUT FAT TABLES
    // Use saved values, NOT new_bpb->...
    uint32_t fat_total_sectors = saved_sectors_fat * saved_fat_count;
    uint32_t fat_start = saved_reserved;
    
    printf("Wiping FAT (%d sectors)... ", fat_total_sectors);
    
    // Allocate 64KB chunks (128 sectors)
    uint32_t chunk_size = 128;
    uint8_t* big_buf = (uint8_t*)dma_alloc(16); // 16 pages
    if (!big_buf) return false;
    memset(big_buf, 0, 4096 * 16);

    for(uint32_t i=0; i < fat_total_sectors; i += chunk_size) {
        uint32_t count = chunk_size;
        if (i + count > fat_total_sectors) count = fat_total_sectors - i;
        
        if (!AhciDriver::getInstance().write(port, fat_start + i, count, big_buf)) {
            printf("\nFAT32: Wipe failed at LBA %d\n", fat_start + i);
            dma_free(big_buf, 16);
            return false;
        }
        
        // Progress Indicator
        if ((i % 2048) == 0) printf("."); 
    }
    printf(" Done.\n");
    dma_free(big_buf, 16);

    // 4. Init FAT Headers (Reserved Entries)
    buf = (uint8_t*)dma_alloc(1);
    memset(buf, 0, 512);
    uint32_t* fat_table = (uint32_t*)buf;
    fat_table[0] = 0x0FFFFFF8;
    fat_table[1] = 0xFFFFFFFF;
    fat_table[2] = 0x0FFFFFFF; // Root Dir EOF
    
    // Use saved_reserved and saved_sectors_fat here too!
    AhciDriver::getInstance().write(port, saved_reserved, 1, buf);
    AhciDriver::getInstance().write(port, saved_reserved + saved_sectors_fat, 1, buf);

    // 5. Zero Root Directory
    uint32_t data_start = saved_reserved + (saved_fat_count * saved_sectors_fat);
    memset(buf, 0, 4096); // Clear Buffer
    AhciDriver::getInstance().write(port, data_start, 8, buf); // 8 sectors = 1 cluster (4KB)

    dma_free(buf, 1);
    printf("FAT32: Format complete.\n");
    return init(port);
}

void Fat32::ls() {
    if (!mounted) {
        printf("Error: File system not mounted.\n");
        return;
    }
    
    uint32_t cluster = root_cluster;
    uint8_t* buf = (uint8_t*)dma_alloc(1); 
    if (!buf) return;
    
    printf("Directory Listing:\n");
    
    while (cluster < 0x0FFFFFF8 && cluster != 0) {
        uint32_t lba = cluster_to_lba(cluster);
        AhciDriver::getInstance().read(port_index, lba, bpb.sectors_per_cluster, buf);
        
        FatDirectoryEntry* entry = (FatDirectoryEntry*)buf;
        for (int i=0; i < (512 * bpb.sectors_per_cluster) / 32; i++) {
            if (entry[i].name[0] == 0x00) break; 
            if (entry[i].name[0] == 0xE5) continue;
            
            if (entry[i].attributes & ATTR_LONG_NAME) continue; 
            if (entry[i].attributes & ATTR_VOLUME_ID) continue;

            char name[12];
            int idx = 0;
            for (int k=0; k<8; k++) {
                if(entry[i].name[k] != ' ') name[idx++] = entry[i].name[k];
            }
            if(entry[i].attributes != ATTR_DIRECTORY) {
                name[idx++] = '.';
                for (int k=0; k<3; k++) {
                    if(entry[i].ext[k] != ' ') name[idx++] = entry[i].ext[k];
                }
            }
            name[idx] = 0;
            
            printf(" %s\t%s\t%d bytes\n", 
                (entry[i].attributes & ATTR_DIRECTORY) ? "<DIR>" : "     ",
                name, entry[i].file_size);
        }
        cluster = get_next_cluster(cluster);
    }
    dma_free(buf, 1);
}

uint32_t Fat32::find_entry(const char* filename, FatDirectoryEntry* out_entry, uint32_t* out_dir_clus, uint32_t* out_offset) {
    if (!mounted) return 0;

    char dos_name[8];
    char dos_ext[3];
    to_dos_filename(filename, dos_name, dos_ext);

    uint32_t cluster = root_cluster;
    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) return 0;
    
    while (cluster < 0x0FFFFFF8 && cluster != 0) {
        uint32_t lba = cluster_to_lba(cluster);
        AhciDriver::getInstance().read(port_index, lba, bpb.sectors_per_cluster, buf);
        
        FatDirectoryEntry* entry = (FatDirectoryEntry*)buf;
        int max_entries = (512 * bpb.sectors_per_cluster) / 32;

        for (int i=0; i < max_entries; i++) {
            if (entry[i].name[0] == 0x00) break; 
            if (entry[i].name[0] == 0xE5) continue;
            if (entry[i].attributes & ATTR_VOLUME_ID) continue;

            if (memcmp(entry[i].name, dos_name, 8) == 0 && 
                memcmp(entry[i].ext, dos_ext, 3) == 0) {
                
                if(out_entry) *out_entry = entry[i];
                if(out_dir_clus) *out_dir_clus = cluster;
                if(out_offset) *out_offset = i; 
                dma_free(buf, 1);
                return (entry[i].cluster_high << 16) | entry[i].cluster_low;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    dma_free(buf, 1);
    return 0;
}

bool Fat32::read_file(const char* filename, void* buffer, uint32_t buffer_len) {
    if (!mounted) return false;

    FatDirectoryEntry entry;
    uint32_t cluster = find_entry(filename, &entry, nullptr, nullptr);
    if (cluster == 0) {
        printf("FAT32: File not found.\n");
        return false;
    }

    if (entry.file_size > buffer_len) {
        printf("FAT32: Buffer too small.\n");
        return false;
    }

    uint8_t* out_ptr = (uint8_t*)buffer;
    uint8_t* temp = (uint8_t*)dma_alloc(1);
    if (!temp) return false;
    
    uint32_t remaining = entry.file_size;
    while (remaining > 0 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        AhciDriver::getInstance().read(port_index, lba, bpb.sectors_per_cluster, temp);
        
        uint32_t chunk = (remaining > 4096) ? 4096 : remaining;
        memcpy(out_ptr, temp, chunk);
        out_ptr += chunk;
        remaining -= chunk;
        cluster = get_next_cluster(cluster);
    }
    dma_free(temp, 1);
    return true;
}

bool Fat32::create_file(const char* filename) {
    if (!mounted) {
        printf("Error: File system not mounted.\n");
        return false;
    }

    if (find_entry(filename, nullptr, nullptr, nullptr) != 0) {
        printf("FAT32: File exists.\n");
        return false;
    }

    FatDirectoryEntry new_ent;
    memset(&new_ent, 0, 32);
    to_dos_filename(filename, new_ent.name, new_ent.ext);
    new_ent.attributes = ATTR_ARCHIVE;
    
    uint32_t new_clus = allocate_cluster();
    if (new_clus == 0) return false;
    
    new_ent.cluster_high = (new_clus >> 16);
    new_ent.cluster_low = (new_clus & 0xFFFF);
    new_ent.file_size = 0;

    uint32_t cluster = root_cluster;
    uint8_t* buf = (uint8_t*)dma_alloc(1);
    if (!buf) return false;
    
    while (cluster < 0x0FFFFFF8 && cluster != 0) {
        AhciDriver::getInstance().read(port_index, cluster_to_lba(cluster), bpb.sectors_per_cluster, buf);
        FatDirectoryEntry* entry = (FatDirectoryEntry*)buf;
        int max_entries = (512 * bpb.sectors_per_cluster) / 32;

        for (int i=0; i < max_entries; i++) {
            if (entry[i].name[0] == 0x00 || entry[i].name[0] == 0xE5) {
                entry[i] = new_ent;
                AhciDriver::getInstance().write(port_index, cluster_to_lba(cluster), bpb.sectors_per_cluster, buf);
                dma_free(buf, 1);
                return true;
            }
        }
        
        uint32_t next = get_next_cluster(cluster);
        if (next >= 0x0FFFFFF8) {
            uint32_t dir_next = allocate_cluster();
            if (dir_next == 0) break;
            set_next_cluster(cluster, dir_next);
            cluster = dir_next;
        } else {
            cluster = next;
        }
    }

    dma_free(buf, 1);
    return false;
}

bool Fat32::write_file(const char* filename, void* data, uint32_t len) {
    if (!mounted) return false;

    FatDirectoryEntry entry;
    uint32_t dir_clus, dir_offset;
    
    uint32_t cluster = find_entry(filename, &entry, &dir_clus, &dir_offset);
    if (cluster == 0) {
        if (!create_file(filename)) return false;
        cluster = find_entry(filename, &entry, &dir_clus, &dir_offset);
    }

    uint8_t* src = (uint8_t*)data;
    uint8_t* temp = (uint8_t*)dma_alloc(1);
    if (!temp) return false;

    uint32_t bytes_left = len;
    uint32_t curr_clus = cluster;
    
    while (bytes_left > 0) {
        uint32_t chunk = (bytes_left > 4096) ? 4096 : bytes_left;
        memset(temp, 0, 4096);
        memcpy(temp, src, chunk);
        
        AhciDriver::getInstance().write(port_index, cluster_to_lba(curr_clus), bpb.sectors_per_cluster, temp);
        
        src += chunk;
        bytes_left -= chunk;
        
        if (bytes_left > 0) {
            uint32_t next = get_next_cluster(curr_clus);
            if (next >= 0x0FFFFFF8) {
                next = allocate_cluster();
                if (next == 0) { dma_free(temp, 1); return false; }
                set_next_cluster(curr_clus, next);
            }
            curr_clus = next;
        }
    }
    
    AhciDriver::getInstance().read(port_index, cluster_to_lba(dir_clus), bpb.sectors_per_cluster, temp);
    FatDirectoryEntry* entries = (FatDirectoryEntry*)temp;
    entries[dir_offset].file_size = len;
    AhciDriver::getInstance().write(port_index, cluster_to_lba(dir_clus), bpb.sectors_per_cluster, temp);

    dma_free(temp, 1);
    return true;
}