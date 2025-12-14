#include "raw_loader.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../memory/vmm.h"
#include "../memory/pmm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"

// Defined in apps/cpl_compiler.cpp
#define HEADER_MAGIC "ChucklesProgram"

// High kernel address for loading
#define KERNEL_PROG_BASE 0xFFFFF00000000000

// ASM helper
extern "C" void call_kernel_program(void* entry_point);

void RawLoader::load_and_run(const char* filename, int argc, char** argv) {
    printf("LOADER: Loading %s into Kernel Space...\n", filename);
    
    // 1. Read file to heap buffer
    uint32_t max_size = 64 * 1024;
    uint8_t* buffer = (uint8_t*)malloc(max_size);
    if (!buffer) {
        printf("LOADER: OOM.\n");
        return;
    }
    
    if (!Fat32::getInstance().read_file(filename, buffer, max_size)) {
        printf("LOADER: File read error.\n");
        free(buffer);
        return;
    }
    
    // 2. Validate Header
    if (memcmp(buffer, HEADER_MAGIC, 15) != 0) {
        printf("LOADER: Invalid Magic. Not a ChucklesProgram.\n");
        free(buffer);
        return;
    }
    
    // 3. Map Executable Kernel Memory
    // 16 pages (64KB) at KERNEL_PROG_BASE, RWX (0x03 in Kernel implies RW, NX absent implies X)
    for(int i=0; i<16; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page(KERNEL_PROG_BASE + (i * 4096), (uint64_t)phys, 0x03); 
        memset((void*)(KERNEL_PROG_BASE + (i * 4096)), 0, 4096);
    }
    
    // 4. Copy Code (Skip header)
    memcpy((void*)KERNEL_PROG_BASE, buffer + 16, max_size - 16);
    
    free(buffer);
    
    printf("LOADER: Executing at %p...\n", (void*)KERNEL_PROG_BASE);
    
    // 5. Execute via Safety Wrapper
    call_kernel_program((void*)KERNEL_PROG_BASE);
    
    printf("LOADER: Program finished.\n");
}