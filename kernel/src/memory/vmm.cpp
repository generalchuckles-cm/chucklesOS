#include "vmm.h"
#include "pmm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h" 

static uint64_t read_cr3() {
    uint64_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static void invlpg(uint64_t addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

void vmm_init() {
    printf("VMM: Initialized (Using Limine PML4)\n");
}

static uint64_t* get_next_level(uint64_t* current_level, uint64_t index, bool alloc) {
    uint64_t entry = current_level[index];

    if (entry & PTE_PRESENT) {
        if (entry & PTE_HUGE) {
            // Cannot split huge pages dynamically here
            return nullptr;
        }
        uint64_t phys = entry & 0x000FFFFFFFFFF000;
        return (uint64_t*)(phys + g_hhdm_offset);
    }

    if (!alloc) return nullptr;

    void* new_table_phys = pmm_alloc(1);
    if (!new_table_phys) {
        return nullptr;
    }

    void* new_table_virt = (void*)((uint64_t)new_table_phys + g_hhdm_offset);
    memset(new_table_virt, 0, PAGE_SIZE);

    current_level[index] = (uint64_t)new_table_phys | PTE_RW | PTE_PRESENT;

    return (uint64_t*)new_table_virt;
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_phys = read_cr3() & 0x000FFFFFFFFFF000;
    uint64_t* pml4 = (uint64_t*)(pml4_phys + g_hhdm_offset);

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level(pml4, pml4_idx, true);
    if (!pdpt) return;
    
    uint64_t* pd = get_next_level(pdpt, pdpt_idx, true);
    if (!pd) return;
    
    uint64_t* pt = get_next_level(pd, pd_idx, true);
    if (!pt) return;

    pt[pt_idx] = (phys & 0x000FFFFFFFFFF000) | flags;
    invlpg(virt);
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_phys = read_cr3() & 0x000FFFFFFFFFF000;
    uint64_t* pml4 = (uint64_t*)(pml4_phys + g_hhdm_offset);

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level(pml4, pml4_idx, false);
    if (!pdpt) return;
    uint64_t* pd = get_next_level(pdpt, pdpt_idx, false);
    if (!pd) return;
    uint64_t* pt = get_next_level(pd, pd_idx, false);
    if (!pt) return;

    pt[pt_idx] = 0;
    invlpg(virt);
}

uint64_t vmm_virt_to_phys(uint64_t virt) {
    uint64_t pml4_phys = read_cr3() & 0x000FFFFFFFFFF000;
    uint64_t* pml4 = (uint64_t*)(pml4_phys + g_hhdm_offset);

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level(pml4, pml4_idx, false);
    if (!pdpt) return 0;

    if (pdpt[pdpt_idx] & PTE_HUGE) {
        uint64_t base = pdpt[pdpt_idx] & 0x000FFFFFC0000000;
        return base + (virt & 0x3FFFFFFF);
    }

    uint64_t* pd = get_next_level(pdpt, pdpt_idx, false);
    if (!pd) return 0;

    if (pd[pd_idx] & PTE_HUGE) {
        uint64_t base = pd[pd_idx] & 0x000FFFFFFFE00000;
        return base + (virt & 0x1FFFFF);
    }

    uint64_t* pt = get_next_level(pd, pd_idx, false);
    if (!pt) return 0;

    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;

    return (pt[pt_idx] & 0x000FFFFFFFFFF000) + (virt & 0xFFF);
}
