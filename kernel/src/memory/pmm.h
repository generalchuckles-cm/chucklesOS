#ifndef PMM_H
#define PMM_H

#include <cstdint>
#include <cstddef>

#define PAGE_SIZE 4096

// Global HHDM Offset (Physical Address + Offset = Virtual Address)
// Exposed so the VMM can read/write page tables.
extern uint64_t g_hhdm_offset;

// Initializes the PMM using the Limine memory map.
void pmm_init();

// Allocates 'count' contiguous pages. Returns Physical Address.
void* pmm_alloc(size_t count);

// Frees pages starting at Physical Address 'ptr'.
void pmm_free(void* ptr, size_t count);

uint64_t pmm_get_total_memory();
uint64_t pmm_get_used_memory();
uint64_t pmm_get_free_memory();

#endif