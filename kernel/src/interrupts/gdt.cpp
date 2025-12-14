#include "gdt.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"

// 0: Null, 1: KCode, 2: KData, 3: UCode, 4: UData, 5: TSS (16 bytes)
static uint8_t gdt_table[64]; 
static TSS tss;
static GDTDescriptor gdtr;

extern "C" void load_gdt(void* gdtr, uint16_t code_sel, uint16_t data_sel, uint16_t tss_sel);

static void encode_entry(uint8_t* target, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    target[0] = (limit >> 0) & 0xFF;
    target[1] = (limit >> 8) & 0xFF;
    target[2] = (base >> 0) & 0xFF;
    target[3] = (base >> 8) & 0xFF;
    target[4] = (base >> 16) & 0xFF;
    target[5] = access;
    target[6] = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    target[7] = (base >> 24) & 0xFF;
}

static void encode_tss(uint8_t* target, uint64_t base, uint32_t limit) {
    encode_entry(target, (uint32_t)base, limit, 0x89, 0x00); // 0x89 = Present, Ring 0, Available TSS
    // Upper 64 bits of TSS entry
    *(uint32_t*)(target + 8) = (base >> 32);
    *(uint32_t*)(target + 12) = 0;
}

void gdt_init() {
    memset(gdt_table, 0, sizeof(gdt_table));
    memset(&tss, 0, sizeof(TSS));

    // 1. Allocate a Safe Stack for Double Faults (16KB)
    void* stack_phys = pmm_alloc(4);
    // IST1 points to the TOP of this stack
    tss.ist1 = (uint64_t)stack_phys + g_hhdm_offset + (4096 * 4);
    tss.iomap_base = sizeof(TSS);

    // 2. Setup GDT Entries
    // 0x00: Null
    // 0x08: Kernel Code
    encode_entry(&gdt_table[8], 0, 0xFFFFF, 0x9A, 0xA0);
    // 0x10: Kernel Data
    encode_entry(&gdt_table[16], 0, 0xFFFFF, 0x92, 0xA0);
    // 0x18: User Code
    encode_entry(&gdt_table[24], 0, 0xFFFFF, 0xFA, 0xA0);
    // 0x20: User Data
    encode_entry(&gdt_table[32], 0, 0xFFFFF, 0xF2, 0xA0);
    // 0x28: TSS
    encode_tss(&gdt_table[40], (uint64_t)&tss, sizeof(TSS)-1);

    // 3. Load
    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base = (uint64_t)&gdt_table;

    load_gdt(&gdtr, 0x08, 0x10, 0x28);
    
    printf("GDT: TSS Installed. Panic Stack @ %p\n", (void*)tss.ist1);
}