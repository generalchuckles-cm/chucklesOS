#include "swp.h"
#include "pmm.h"
#include "vmm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../cppstd/stdlib.h" // for atoi if needed, though we implement custom parse

SwapManager& SwapManager::getInstance() {
    static SwapManager instance;
    return instance;
}

SwapManager::SwapManager() : is_active(false), current_size(0), page_count(0) {}

uint64_t SwapManager::parse_size(const char* str) {
    uint64_t num = 0;
    int i = 0;
    
    // Parse number part
    while (str[i] >= '0' && str[i] <= '9') {
        num = num * 10 + (str[i] - '0');
        i++;
    }

    if (num == 0) return 0;

    // Parse suffix
    char suffix = str[i];
    if (suffix == 'K' || suffix == 'k') {
        num *= 1024;
    } else if (suffix == 'M' || suffix == 'm') {
        num *= 1024 * 1024;
    } else if (suffix == 'G' || suffix == 'g') {
        num *= 1024 * 1024 * 1024;
    }
    
    // Align to page size (4096)
    if (num % 4096 != 0) {
        num = (num + 4095) & ~4095;
    }

    return num;
}

bool SwapManager::allocate_swap(const char* size_str) {
    if (is_active) {
        printf("SWAP: Error - Swap already allocated. Free it first.\n");
        return false;
    }

    uint64_t bytes = parse_size(size_str);
    if (bytes == 0) {
        printf("SWAP: Invalid size format. Use 512M, 1G, etc.\n");
        return false;
    }

    uint64_t pages_needed = bytes / 4096;
    uint64_t free_ram = pmm_get_free_memory();

    printf("SWAP: Requesting %d MB (%d pages)...\n", (int)(bytes/1024/1024), (int)pages_needed);

    // Safety check (leave at least 16MB for the system)
    if (bytes + (16*1024*1024) > free_ram) {
        printf("SWAP: Not enough physical RAM. (Free: %d MB)\n", (int)(free_ram/1024/1024));
        return false;
    }

    // Allocate and Map
    uint64_t virt_addr = SWAP_VIRT_BASE;
    uint64_t allocated = 0;

    for (uint64_t i = 0; i < pages_needed; i++) {
        void* phys = pmm_alloc(1);
        if (!phys) {
            printf("SWAP: OOM during allocation loop at page %d!\n", (int)i);
            // Ideally we should rollback here, but for now we just stop.
            return false;
        }

        // Map SWAP_VIRT_BASE + offset -> Physical Frame
        // Flags: Present | ReadWrite | NoExecute
        vmm_map_page(virt_addr, (uint64_t)phys, PTE_PRESENT | PTE_RW | PTE_NX);
        
        virt_addr += 4096;
        allocated++;
        
        // Progress bar for large allocations
        if (pages_needed > 10000 && (i % 5000 == 0)) {
            printf(".");
        }
    }

    if (pages_needed > 10000) printf("\n");

    current_size = bytes;
    page_count = allocated;
    is_active = true;

    printf("SWAP: Successfully allocated %d MB at Virtual %p\n", 
        (int)(bytes/1024/1024), (void*)SWAP_VIRT_BASE);
        
    return true;
}

void SwapManager::free_swap() {
    if (!is_active) return;

    printf("SWAP: Freeing memory...\n");
    uint64_t virt_addr = SWAP_VIRT_BASE;

    for (uint64_t i = 0; i < page_count; i++) {
        // Get physical address to free it in PMM
        uint64_t phys = vmm_virt_to_phys(virt_addr);
        if (phys) {
            pmm_free((void*)phys, 1);
        }
        
        // Unmap from Page Table
        vmm_unmap_page(virt_addr);
        virt_addr += 4096;
    }

    is_active = false;
    current_size = 0;
    page_count = 0;
    printf("SWAP: Freed.\n");
}

void SwapManager::test_swap() {
    if (!is_active) {
        printf("SWAP: Not allocated.\n");
        return;
    }

    volatile uint64_t* start = (uint64_t*)SWAP_VIRT_BASE;
    volatile uint64_t* end = (uint64_t*)(SWAP_VIRT_BASE + current_size - 8);

    printf("SWAP: Testing R/W...\n");
    
    // Write patterns
    *start = 0xDEADBEEFCAFEBABE;
    *end   = 0x1234567890ABCDEF;

    // Read back
    if (*start == 0xDEADBEEFCAFEBABE && *end == 0x1234567890ABCDEF) {
        printf("SWAP: Verification PASSED.\n");
    } else {
        printf("SWAP: Verification FAILED! (Mem: %lx, %lx)\n", *start, *end);
    }
}