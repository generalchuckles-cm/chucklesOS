#include "smp.h"
#include <limine.h>
#include "../cppstd/stdio.h"
#include "../interrupts/gdt.h"
#include "../interrupts/idt.h"
#include "../memory/vmm.h"
#include "../sys/system_stats.h" 

// Tell Limine we want MP info (Protocol V2+ naming)
__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
    .flags = 0 // X2APIC optional
};

// This function runs on EVERY Application Processor (AP)
void smp_ap_entry(struct limine_mp_info* info) {
    // 1. Initialize per-core GDT/TSS (Fixes Triple Fault)
    gdt_init_ap();
    
    // 2. Load IDT (Read-only, shared is fine)
    idt_reload();
    
    // 3. Enable SSE
    uint64_t cr0, cr4;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); cr0 |= (1 << 1);  
    asm volatile ("mov %0, %%cr0" :: "r"(cr0));
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (3 << 9);  
    asm volatile ("mov %0, %%cr4" :: "r"(cr4));

    // Optional: Print status (Locking handles concurrency)
    // printf("SMP: Core %d online!\n", (int)info->processor_id);

    // 4. Halt loop (Activity Counter)
    while (true) {
        if (info->processor_id < 32) {
            // Explicit read-modify-write to satisfy volatile
            uint64_t current = SystemStats::getInstance().cpu_ticks[info->processor_id];
            SystemStats::getInstance().cpu_ticks[info->processor_id] = current + 1;
        }
        
        asm volatile("hlt");
    }
}

void smp_init() {
    struct limine_mp_response* response = mp_request.response;
    
    if (response == nullptr) {
        printf("SMP: Limine MP response not found.\n");
        return;
    }

    uint64_t cpu_count = response->cpu_count;
    uint64_t bsp_lapic_id = response->bsp_lapic_id;

    SystemStats::getInstance().cpu_count = (int)cpu_count;
    if (cpu_count > 1) SystemStats::getInstance().service_smp_active = true;

    printf("SMP: Found %d CPUs. BSP LAPIC: %d\n", (int)cpu_count, (int)bsp_lapic_id);

    for (uint64_t i = 0; i < cpu_count; i++) {
        struct limine_mp_info* cpu = response->cpus[i];
        
        if (cpu->lapic_id == bsp_lapic_id) {
            continue; 
        }

        cpu->goto_address = smp_ap_entry;
    }
}