#include "gdt.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../sys/spinlock.h"

// MAX CORES SUPPORTED
#define MAX_CORES 32

// GDT Table: 5 entries + (2 entries * 32 cores for TSS)
// 0: Null, 1: KCode, 2: KData, 3: UCode, 4: UData
// 5+: TSS descriptors
static uint64_t gdt_table[5 + (2 * MAX_CORES)]; 

// TSS Structures (One per core)
static TSS tss[MAX_CORES];

static GDTDescriptor gdtr;
static Spinlock gdt_lock;

// Helper to load GDT (defined in interrupts.asm)
extern "C" void load_gdt(void* gdtr, uint16_t code_sel, uint16_t data_sel, uint16_t tss_sel);

static void encode_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    uint8_t* target = (uint8_t*)&gdt_table[index];
    
    target[0] = (limit >> 0) & 0xFF;
    target[1] = (limit >> 8) & 0xFF;
    target[2] = (base >> 0) & 0xFF;
    target[3] = (base >> 8) & 0xFF;
    target[4] = (base >> 16) & 0xFF;
    target[5] = access;
    target[6] = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    target[7] = (base >> 24) & 0xFF;
}

static void encode_tss(int index, uint64_t base, uint32_t limit) {
    uint8_t* target = (uint8_t*)&gdt_table[index];
    
    // Lower 8 bytes (Standard GDT Entry)
    // 0x89 = Present, Ring 0, Available 64-bit TSS (Low)
    encode_entry(index, (uint32_t)base, limit, 0x89, 0x00); 
    
    // Upper 8 bytes (High 32 bits of Base)
    // In x86_64, system descriptors are 16 bytes.
    // The previous encode_entry wrote 8 bytes. We write the next 8 manually.
    uint32_t base_hi = (uint32_t)(base >> 32);
    *(uint32_t*)(target + 8) = base_hi;
    *(uint32_t*)(target + 12) = 0; // Reserved/Zero
}

void gdt_init() {
    ScopedLock lock(gdt_lock);
    
    memset(gdt_table, 0, sizeof(gdt_table));
    memset(tss, 0, sizeof(tss));

    // 1. Setup Standard GDT Entries (Shared by all)
    // 0x08: Kernel Code
    encode_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    // 0x10: Kernel Data
    encode_entry(2, 0, 0xFFFFF, 0x92, 0xA0);
    // 0x18: User Code
    encode_entry(3, 0, 0xFFFFF, 0xFA, 0xA0);
    // 0x20: User Data
    encode_entry(4, 0, 0xFFFFF, 0xF2, 0xA0);

    // 2. Setup BSP TSS (Core 0)
    // Allocate Panic Stack
    void* stack_phys = pmm_alloc(4);
    uint64_t stack_virt = (uint64_t)stack_phys + g_hhdm_offset + (4096 * 4);
    
    tss[0].ist1 = stack_virt;
    tss[0].rsp0 = stack_virt; // FIX: Initialize Kernel Stack for Interrupts/Syscalls
    tss[0].iomap_base = sizeof(TSS);
    
    // TSS is at Index 5 (Selector 0x28)
    encode_tss(5, (uint64_t)&tss[0], sizeof(TSS)-1);

    // 3. Load GDT for BSP
    // Limit = Size in bytes - 1.
    // We only load enough for the BSP initially, or the full table.
    // Let's load the full table limit to be safe for APs.
    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base = (uint64_t)&gdt_table;

    load_gdt(&gdtr, 0x08, 0x10, 0x28);
    
    printf("GDT: BSP Initialized. TSS @ 0x28\n");
}

// Called by APs. 
// Uses the processor ID (LAPIC ID / Index) to select a unique TSS.
// We must derive the index locally or assume a sequential ID.
// For simplicity, we implement a counter here, but ideally we'd pass the CPU ID.
// Since smp_ap_entry doesn't pass args to this func easily, we use a simple atomic counter.
static volatile int g_ap_count = 0;

void gdt_init_ap() {
    ScopedLock lock(gdt_lock);
    
    // Increment core count (BSP is 0, first AP is 1)
    int core_id = ++g_ap_count;
    
    if (core_id >= MAX_CORES) {
        // Too many cores, just halt or reuse 0 (dangerous)
        while(1) asm("hlt");
    }

    // 1. Setup TSS for this Core
    void* stack_phys = pmm_alloc(4);
    uint64_t stack_virt = (uint64_t)stack_phys + g_hhdm_offset + (4096 * 4);

    tss[core_id].ist1 = stack_virt;
    tss[core_id].rsp0 = stack_virt; // FIX: Initialize Kernel Stack for APs
    tss[core_id].iomap_base = sizeof(TSS);
    
    // 2. Add to GDT
    // Indices: 0..4 are static.
    // Core 0 TSS = Index 5
    // Core 1 TSS = Index 7 (TSS is 16 bytes = 2 entries)
    // Core N TSS = Index 5 + (N * 2)
    int tss_index = 5 + (core_id * 2);
    uint16_t tss_selector = tss_index * 8;
    
    encode_tss(tss_index, (uint64_t)&tss[core_id], sizeof(TSS)-1);
    
    // 3. Load GDT
    // We reuse the global GDTR since the table is shared, just the TSS selector differs.
    load_gdt(&gdtr, 0x08, 0x10, tss_selector);
}