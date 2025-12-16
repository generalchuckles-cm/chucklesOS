#include "vmm.h"
#include "pmm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h" 

// Helper to access CPU CR3 register
static uint64_t read_cr3() {
    uint64_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

// Invalidate TLB for a specific address
static void invlpg(uint64_t addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

void vmm_init() {
    // We stick to the Limine-provided page table for now.
    printf("VMM: Initialized (Using existing PML4)\n");
}

// Returns the pointer to the next level table (Virtual Address).
// If alloc is true, it creates the table if missing.
static uint64_t* get_next_level(uint64_t* current_level, uint64_t index, bool alloc) {
    uint64_t entry = current_level[index];

    // Check if the entry is present
    if (entry & PTE_PRESENT) {
        // CRITICAL FIX: Check for Huge Page bit (Bit 7).
        // If we encounter a Huge Page while trying to traverse down, 
        // we essentially hit a "leaf" where we expected a "branch".
        // For this simple OS, we will DESTROY the huge page mapping 
        // to allow allocating a granular 4KB page table here.
        // This creates a "hole" in the identity map, which is acceptable 
        // for remapping user space over unused lower RAM.
        if (entry & PTE_HUGE) {
            // Only clear if we intend to allocate (modify)
            if (alloc) {
                // printf("VMM: Breaking Huge Page at index %d\n", (int)index);
                current_level[index] = 0;
                // Fall through to allocation logic below
            } else {
                // If not allocating, we can't traverse deeper into a huge page
                // without complex splitting logic. Return null.
                return nullptr;
            }
        } else {
            // Normal Page Table pointer. Mask flags to get physical address.
            uint64_t phys = entry & 0x000FFFFFFFFFF000;
            return (uint64_t*)(phys + g_hhdm_offset);
        }
    }

    if (!alloc) return nullptr;

    // Allocate a new table
    void* new_table_phys = pmm_alloc(1); // 1 page
    if (!new_table_phys) {
        printf("VMM: PANIC! OOM while allocating page table.\n");
        while(1) asm("hlt");
    }

    // Zero it out (crucial!)
    void* new_table_virt = (void*)((uint64_t)new_table_phys + g_hhdm_offset);
    memset(new_table_virt, 0, PAGE_SIZE);

    // Write the entry into the current level
    // User | RW | Present
    current_level[index] = (uint64_t)new_table_phys | PTE_USER | PTE_RW | PTE_PRESENT;

    return (uint64_t*)new_table_virt;
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    // 1. Get PML4 (Top Level)
    uint64_t pml4_phys = read_cr3() & 0x000FFFFFFFFFF000;
    uint64_t* pml4 = (uint64_t*)(pml4_phys + g_hhdm_offset);

    // 2. Calculate Indices
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    // 3. Walk the tree
    uint64_t* pdpt = get_next_level(pml4, pml4_idx, true);
    if (!pdpt) return; // Should panic
    
    uint64_t* pd   = get_next_level(pdpt, pdpt_idx, true);
    if (!pd) return;
    
    uint64_t* pt   = get_next_level(pd, pd_idx, true);
    if (!pt) return;

    // 4. Set the Page Table Entry
    pt[pt_idx] = phys | flags;

    // 5. Invalidate TLB
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

    // Clear presence bit
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
    
    // Check if PDPT entry is Huge (1GB page)
    if (pml4[pml4_idx] & PTE_HUGE) {
         uint64_t base = pml4[pml4_idx] & 0x000FFFFFC0000000; // 1GB align
         return base + (virt & 0x3FFFFFFF);
    }

    uint64_t* pd = get_next_level(pdpt, pdpt_idx, false);
    if (!pd) return 0;
    
    // Check if PD entry is Huge (2MB page)
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        uint64_t base = pdpt[pdpt_idx] & 0x000FFFFFFFE00000; // 2MB align mask
        return base + (virt & 0x1FFFFF);
    }

    uint64_t* pt = get_next_level(pd, pd_idx, false);
    if (!pt) return 0;

    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;

    return (pt[pt_idx] & 0x000FFFFFFFFFF000) + (virt & 0xFFF);
}