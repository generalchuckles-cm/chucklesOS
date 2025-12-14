#include "elf_loader.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../memory/vmm.h"
#include "../memory/pmm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"

// CXE Header Definition
struct CXEHeader {
    uint32_t magic;      
    uint32_t entry;      
    uint32_t text_len;   
    uint32_t data_len;   
};

extern "C" void jump_to_user_program(uint64_t entry, uint64_t argc, uint64_t argv, uint64_t stack_top);

void ElfLoader::load_and_run(const char* filename, int argc, char** argv) {
    printf("LOADER: Loading %s...\n", filename);
    
    // 1. Read the file into a temporary kernel buffer
    // Limit to 256KB for this simple loader
    uint32_t file_size = 256 * 1024; 
    uint8_t* file_buffer = (uint8_t*)malloc(file_size);
    if (!file_buffer) {
        printf("LOADER: OOM buffer.\n");
        return;
    }
    
    if (!Fat32::getInstance().read_file(filename, file_buffer, file_size)) {
        printf("LOADER: File not found.\n");
        free(file_buffer);
        return;
    }
    
    CXEHeader* hdr = (CXEHeader*)file_buffer;
    
    // 2. Validate Magic "CXE\0" (0x00455843)
    if (hdr->magic != 0x00455843) {
        printf("LOADER: Invalid CXE Format (Magic: %x)\n", hdr->magic);
        free(file_buffer);
        return;
    }
    
    printf("LOADER: Text: %d bytes, Data: %d bytes, Entry: 0x%x\n", 
           hdr->text_len, hdr->data_len, hdr->entry);
    
    // 3. Map Text Section at 0x401000
    // The compiled code assumes Text starts at 0x401000 and Data at 0x402000
    uint64_t text_vaddr = 0x401000;
    uint32_t text_pages = (hdr->text_len + 4095) / 4096;
    if (text_pages == 0) text_pages = 1;
    
    for(uint32_t i=0; i<text_pages; i++) {
        void* phys = pmm_alloc(1);
        if (!phys) { printf("LOADER: OOM Physical.\n"); free(file_buffer); return; }
        
        // CRITICAL FIX: PTE_RW is required to copy the code into place.
        // For simplicity, we leave it writable.
        vmm_map_page(text_vaddr + i*4096, (uint64_t)phys, PTE_PRESENT | PTE_RW | PTE_USER);
        memset((void*)(text_vaddr + i*4096), 0, 4096);
    }
    
    // Copy Text from file buffer
    // Offset in file = sizeof(CXEHeader)
    if (hdr->text_len > 0) {
        memcpy((void*)text_vaddr, file_buffer + sizeof(CXEHeader), hdr->text_len);
    }
    
    // 4. Map Data Section at 0x402000
    uint64_t data_vaddr = 0x402000;
    uint32_t data_pages = (hdr->data_len + 4095) / 4096;
    if (data_pages == 0) data_pages = 1;
    
    for(uint32_t i=0; i<data_pages; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page(data_vaddr + i*4096, (uint64_t)phys, PTE_PRESENT | PTE_RW | PTE_USER);
        memset((void*)(data_vaddr + i*4096), 0, 4096);
    }
    
    // Copy Data from file buffer
    // Offset in file = sizeof(CXEHeader) + hdr->text_len
    if (hdr->data_len > 0) {
        memcpy((void*)data_vaddr, file_buffer + sizeof(CXEHeader) + hdr->text_len, hdr->data_len);
    }
    
    // 5. Setup User Stack at 0x70000000 (grows down from 0x70008000)
    uint64_t stack_base = 0x70000000;
    uint64_t stack_pages = 8;
    for(uint64_t i=0; i<stack_pages; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page(stack_base + i*4096, (uint64_t)phys, PTE_PRESENT | PTE_RW | PTE_USER);
        memset((void*)(stack_base + i*4096), 0, 4096);
    }
    
    // Stack Top (16-byte aligned)
    uint64_t stack_top = stack_base + (stack_pages * 4096);
    
    printf("LOADER: Jumping to User Mode (0x%x)...\n", hdr->entry);
    
    // Free the kernel buffer before jumping to save memory
    free(file_buffer);
    
    // Flush TLB to ensure mappings take effect immediately
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax");

    // 6. Jump!
    jump_to_user_program(hdr->entry, (uint64_t)argc, (uint64_t)argv, stack_top);
    
    // Should never return
    printf("LOADER: User program returned.\n");
}