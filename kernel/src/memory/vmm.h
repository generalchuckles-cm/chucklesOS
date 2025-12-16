#ifndef VMM_H
#define VMM_H

#include <cstdint>

// Page Table Entry Flags
#define PTE_PRESENT   (1ULL << 0)
#define PTE_RW        (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_PWT       (1ULL << 3) // Write-Through
#define PTE_PCD       (1ULL << 4) // Cache Disable
#define PTE_ACCESSED  (1ULL << 5)
#define PTE_DIRTY     (1ULL << 6)
#define PTE_HUGE      (1ULL << 7) // Huge Page (2MB/1GB)
#define PTE_NX        (1ULL << 63)// No Execute

// Initialize VMM (stores current PML4)
void vmm_init();

// Map a specific virtual page to a physical frame
// virt: Virtual address (must be page aligned)
// phys: Physical address (must be page aligned)
// flags: PTE flags
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

// Unmap a page
void vmm_unmap_page(uint64_t virt);

// Helper: Get physical address of a virtual one
uint64_t vmm_virt_to_phys(uint64_t virt);

#endif