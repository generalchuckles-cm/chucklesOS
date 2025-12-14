#ifndef HEAP_H
#define HEAP_H

#include <cstddef>
#include <cstdint>

// Virtual Address where the Heap starts.
// We pick a high address safe from the kernel code (0xffffffff80...)
// 0xffff_c000_0000_0000 is a nice clean spot in the higher half.
#define HEAP_START_ADDR 0xFFFFC00000000000

void heap_init();

// Standard Library Allocator Interface
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t new_size);

// --- Getters for Live Stats ---
size_t heap_get_used();
size_t heap_get_total();

// Debugging
void heap_print_stats();

#endif