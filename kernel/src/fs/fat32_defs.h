#ifndef FAT32_DEFS_H
#define FAT32_DEFS_H

#include <cstdint>

#define FAT32_SIGNATURE     0x29
#define FAT32_ENTRY_FREE    0x00000000
#define FAT32_ENTRY_EOC     0x0FFFFFF8
#define FAT32_BAD_CLUSTER   0x0FFFFFF7

#define ATTR_READ_ONLY      0x01
#define ATTR_HIDDEN         0x02
#define ATTR_SYSTEM         0x04
#define ATTR_VOLUME_ID      0x08
#define ATTR_DIRECTORY      0x10
#define ATTR_ARCHIVE        0x20
#define ATTR_LONG_NAME      (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

#pragma pack(push, 1)

struct Fat32BootSector {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count; // 0 for FAT32
    uint16_t total_sectors_16; // 0 for FAT32
    uint8_t  media_type;
    uint16_t sectors_per_fat_16; // 0 for FAT32
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 Extended Fields
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8]; // "FAT32   "
} __attribute__((packed));

struct FatDirectoryEntry {
    char     name[8];
    char     ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct FSInfo {
    uint32_t lead_sig;       // 0x41615252
    uint8_t  reserved1[480];
    uint32_t struct_sig;     // 0x61417272
    uint32_t free_count;     // -1 if unknown
    uint32_t next_free;      // Hint
    uint8_t  reserved2[12];
    uint32_t trail_sig;      // 0xAA550000
} __attribute__((packed));

#pragma pack(pop)

#endif