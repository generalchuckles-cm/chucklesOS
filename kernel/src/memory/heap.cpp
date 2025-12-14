#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"

#define HEAP_MAGIC 0xC0FFEE
#define HEAP_INITIAL_PAGES 16 

struct HeapHeader {
    size_t length; // Size of data
    HeapHeader* next;
    HeapHeader* prev;
    bool is_free;
    uint32_t magic; 
};

// Global Heap State
static HeapHeader* first_block = nullptr;
static uint64_t heap_end_virt = HEAP_START_ADDR;

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

static void heap_expand(size_t size_needed) {
    size_t pages_needed = (size_needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed == 0) pages_needed = 1;

    for (size_t i = 0; i < pages_needed; i++) {
        void* phys = pmm_alloc(1);
        if (!phys) {
            printf("HEAP: PANIC! Out of Physical RAM.\n");
            return; 
        }
        
        vmm_map_page(heap_end_virt, (uint64_t)phys, PTE_PRESENT | PTE_RW | PTE_NX);
        heap_end_virt += PAGE_SIZE;
    }

    HeapHeader* new_block = nullptr;
    
    if (!first_block) {
        new_block = (HeapHeader*)HEAP_START_ADDR;
        new_block->prev = nullptr;
        first_block = new_block;
    } else {
        HeapHeader* current = first_block;
        while (current->next) {
            current = current->next;
        }
        
        new_block = (HeapHeader*)((uint8_t*)current + sizeof(HeapHeader) + current->length);
        current->next = new_block;
        new_block->prev = current;
    }

    new_block->length = (pages_needed * PAGE_SIZE) - sizeof(HeapHeader);
    new_block->next = nullptr;
    new_block->is_free = true;
    new_block->magic = HEAP_MAGIC;
}

void heap_init() {
    heap_expand(HEAP_INITIAL_PAGES * PAGE_SIZE);
    printf("HEAP: Initialized at %p\n", (void*)HEAP_START_ADDR);
}

static void split_block(HeapHeader* block, size_t size) {
    size_t remaining = block->length - size - sizeof(HeapHeader);
    
    if (remaining > 32) {
        HeapHeader* new_split = (HeapHeader*)((uint8_t*)block + sizeof(HeapHeader) + size);
        
        new_split->length = remaining;
        new_split->is_free = true;
        new_split->magic = HEAP_MAGIC;
        new_split->next = block->next;
        new_split->prev = block;
        
        if (block->next) {
            block->next->prev = new_split;
        }
        
        block->next = new_split;
        block->length = size;
    }
}

static void coalesce(HeapHeader* block) {
    if (block->next && block->next->is_free) {
        block->length += sizeof(HeapHeader) + block->next->length;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
    
    if (block->prev && block->prev->is_free) {
        block->prev->length += sizeof(HeapHeader) + block->length;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void* malloc(size_t size) {
    if (size == 0) return nullptr;

    size = align_up(size, 16);

    HeapHeader* current = first_block;
    
    while (current) {
        if (current->magic != HEAP_MAGIC) {
            printf("HEAP: CORRUPTION DETECTED at %p\n", current);
            return nullptr;
        }

        if (current->is_free && current->length >= size) {
            current->is_free = false;
            split_block(current, size);
            return (void*)((uint8_t*)current + sizeof(HeapHeader));
        }
        
        if (current->next == nullptr) {
            heap_expand(size + sizeof(HeapHeader));
        }
        
        current = current->next;
    }

    return nullptr;
}

void free(void* ptr) {
    if (!ptr) return;

    HeapHeader* header = (HeapHeader*)((uint8_t*)ptr - sizeof(HeapHeader));
    
    if (header->magic != HEAP_MAGIC) {
        printf("HEAP: Double free or corruption at %p\n", ptr);
        return;
    }

    header->is_free = true;
    coalesce(header);
}

void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }

    HeapHeader* header = (HeapHeader*)((uint8_t*)ptr - sizeof(HeapHeader));
    if (header->length >= new_size) {
        return ptr; 
    }

    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, header->length);
        free(ptr);
    }
    return new_ptr;
}

size_t heap_get_used() {
    HeapHeader* curr = first_block;
    size_t used = 0;
    while (curr) {
        if (!curr->is_free) {
            used += curr->length;
        }
        // Also count the header itself as used memory
        used += sizeof(HeapHeader);
        curr = curr->next;
    }
    return used;
}

size_t heap_get_total() {
    // The total size of the mapped heap region
    if (heap_end_virt <= HEAP_START_ADDR) {
        return 0;
    }
    return heap_end_virt - HEAP_START_ADDR;
}

void heap_print_stats() {
    HeapHeader* curr = first_block;
    size_t used = 0;
    size_t free_mem = 0;
    int blocks = 0;

    printf("--- Heap Stats ---\n");
    while (curr) {
        if (curr->is_free) free_mem += curr->length;
        else used += curr->length;
        blocks++;
        curr = curr->next;
    }
    printf("Used: %d bytes\n", (int)used);
    printf("Free: %d bytes\n", (int)free_mem);
    printf("Blocks: %d\n", blocks);
}

// --- C++ Operators ---

void* operator new(size_t size) {
    return malloc(size);
}

void* operator new[](size_t size) {
    return malloc(size);
}

void operator delete(void* p) {
    free(p);
}

void operator delete[](void* p) {
    free(p);
}

void operator delete(void* p, size_t size) {
    (void)size;
    free(p);
}

// ADDED: Sized array delete
void operator delete[](void* p, size_t size) {
    (void)size;
    free(p);
}