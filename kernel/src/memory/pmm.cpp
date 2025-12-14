#include "pmm.h"
#include <limine.h>
#include "../cppstd/stdio.h"
#include "../cppstd/string.h" 

// Limine Memory Map Request
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

// HHDM Request
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

static uint8_t* bitmap = nullptr;
static uint64_t bitmap_size = 0; 
static uint64_t highest_addr = 0;
static uint64_t total_ram = 0;
static uint64_t used_ram = 0;

// Global HHDM Offset
uint64_t g_hhdm_offset = 0; 

static void bitmap_set(uint64_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static void bitmap_unset(uint64_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static bool bitmap_test(uint64_t bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) > 0;
}

void pmm_init() {
    struct limine_memmap_response* memmap = memmap_request.response;
    struct limine_hhdm_response* hhdm = hhdm_request.response;

    if (!memmap || !hhdm) {
        printf("PMM: Critical Error - Limine requests failed.\n");
        return;
    }

    g_hhdm_offset = hhdm->offset;

    // 1. Calculate Total RAM and Highest Address
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        
        if (entry->type == LIMINE_MEMMAP_USABLE || 
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            total_ram += entry->length;
        }

        uint64_t top = entry->base + entry->length;
        if (top > highest_addr) highest_addr = top;
    }

    // 2. Calculate Bitmap Size needed
    uint64_t total_pages = highest_addr / PAGE_SIZE;
    bitmap_size = total_pages / 8;
    if (total_pages % 8) bitmap_size++;

    // 3. Find a place to put the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length >= bitmap_size) {
                // IMPORTANT: Convert Physical Address to Virtual Address!
                bitmap = (uint8_t*)(entry->base + g_hhdm_offset);
                memset(bitmap, 0xFF, bitmap_size); 
                break;
            }
        }
    }

    if (!bitmap) {
        printf("PMM: Critical Error - Could not find RAM for bitmap!\n");
        return;
    }

    // 4. Populate the bitmap based on the memory map
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE || 
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            
            for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE) {
                uint64_t phys_addr = entry->base + j;
                bitmap_unset(phys_addr / PAGE_SIZE);
            }
        }
    }

    // 5. Mark the Bitmap ITSELF as used!
    uint64_t bitmap_phys = (uint64_t)bitmap - g_hhdm_offset;
    uint64_t bitmap_start_page = bitmap_phys / PAGE_SIZE;
    uint64_t bitmap_pages = bitmap_size / PAGE_SIZE + 1;
    
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        bitmap_set(bitmap_start_page + i);
    }

    // 6. Mark 0x0 -> 0x100000 (First 1MB) as used
    for (uint64_t i = 0; i < 256; i++) {
        bitmap_set(i);
    }

    // 7. Calculate Stats
    uint64_t free_pages = 0;
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            free_pages++;
        }
    }
    
    uint64_t free_ram_bytes = free_pages * PAGE_SIZE;
    used_ram = total_ram - free_ram_bytes;

    printf("PMM: Initialized.\n");
    printf("PMM: HHDM Offset 0x%p\n", (void*)g_hhdm_offset);
    printf("PMM: Bitmap at Virt 0x%p (Phys 0x%x)\n", bitmap, bitmap_phys);
    printf("PMM: Detected %d MB RAM (%d MB Free)\n", 
        (int)(total_ram / 1024 / 1024), 
        (int)(free_ram_bytes / 1024 / 1024));
}

void* pmm_alloc(size_t count) {
    if (count == 0) return nullptr;

    uint64_t total_pages = highest_addr / PAGE_SIZE;
    uint64_t consecutive = 0;
    uint64_t start_idx = 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) start_idx = i;
            consecutive++;
            
            if (consecutive == count) {
                for (uint64_t j = 0; j < count; j++) {
                    bitmap_set(start_idx + j);
                }
                used_ram += count * PAGE_SIZE;
                // Return PHYSICAL address
                return (void*)(start_idx * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }

    return nullptr;
}

void pmm_free(void* ptr, size_t count) {
    if (!ptr) return;
    
    uint64_t start_page = (uint64_t)ptr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        bitmap_unset(start_page + i);
    }
    used_ram -= count * PAGE_SIZE;
}

uint64_t pmm_get_total_memory() { return total_ram; }
uint64_t pmm_get_used_memory() { return used_ram; }
uint64_t pmm_get_free_memory() { return total_ram - used_ram; }