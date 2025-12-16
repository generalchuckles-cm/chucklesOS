#include "high_loader.h"
#include "../fs/fat32.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../memory/heap.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"

// Defined in interrupts.asm
extern "C" void jump_to_user_program(uint64_t entry, uint64_t argc, uint64_t argv, uint64_t stack_top);

void HighLoader::load_and_run(const char* filename) {
    printf("LOADER: High-Load request for %s\n", filename);

    // 1. Read file into temporary kernel buffer
    uint32_t file_size = 64 * 1024; // Limit to 64KB for now
    uint8_t* temp_buf = (uint8_t*)malloc(file_size);
    if (!temp_buf) {
        printf("LOADER: OOM temp buffer.\n");
        return;
    }
    memset(temp_buf, 0, file_size); // Ensure clean buffer

    if (!Fat32::getInstance().read_file(filename, temp_buf, file_size)) {
        printf("LOADER: File not found.\n");
        free(temp_buf);
        return;
    }

    // Determine actual size
    uint32_t actual_size = file_size;

    // 2. Calculate Top-Down Address in Physical RAM
    uint64_t total_ram = pmm_get_total_memory();
    // Leave 1MB safety margin from very top (for BIOS/ACPI reclaim)
    uint64_t top_safe_ram = total_ram - (1024 * 1024); 
    
    // Calculate start address (Page Aligned)
    uint64_t load_phys = (top_safe_ram - actual_size) & ~0xFFF;
    
    // 3. Map this memory to Virtual Address
    // Using 0x00400000 (4MB) as base address.
    uint64_t virt_base = 0x00400000;
    uint32_t pages = (actual_size + 4095) / 4096;

    printf("LOADER: Loading at Phys 0x%lx -> Virt 0x%lx\n", load_phys, virt_base);

    // Map pages. Note: vmm_map_page now handles breaking huge pages.
    for (uint32_t i = 0; i < pages; i++) {
        vmm_map_page(virt_base + (i * 4096), load_phys + (i * 4096), PTE_PRESENT | PTE_RW | PTE_USER);
    }

    // 4. Copy Code to the physical location
    // We rely on the HHDM (Higher Half Direct Map) to write to physical
    extern uint64_t g_hhdm_offset;
    void* write_ptr = (void*)(load_phys + g_hhdm_offset);
    memcpy(write_ptr, temp_buf, actual_size);
    
    free(temp_buf);

    // 5. Setup User Stack (Growing down from Virtual Base)
    // We map 4 pages below the code for stack (0x003FC000 to 0x00400000)
    uint64_t stack_top = virt_base; 
    uint64_t stack_bottom = stack_top - (4 * 4096);
    
    for (uint64_t addr = stack_bottom; addr < stack_top; addr += 4096) {
        void* s_phys = pmm_alloc(1);
        vmm_map_page(addr, (uint64_t)s_phys, PTE_PRESENT | PTE_RW | PTE_USER);
    }

    printf("LOADER: Launching...\n");
    
    // 6. Jump to 0x00400000 (Entry point of the raw binary)
    jump_to_user_program(virt_base, 0, 0, stack_top);
}