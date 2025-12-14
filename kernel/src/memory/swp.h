#ifndef SWP_H
#define SWP_H

#include <cstdint>
#include <cstddef>

// We place the swap region at a very high virtual address
// Heap starts at 0xFFFF_C000..., so let's put Swap at 0xFFFF_E000...
#define SWAP_VIRT_BASE 0xFFFFE00000000000

class SwapManager {
public:
    static SwapManager& getInstance();

    // Allocates a region of memory.
    // size_str: e.g., "512M", "1G", "4096K"
    bool allocate_swap(const char* size_str);

    // Frees the swap region (returns pages to PMM)
    void free_swap();

    // Verification method to prove memory is mapped and writable
    void test_swap();

    uint64_t getSize() const { return current_size; }
    bool isActive() const { return is_active; }

private:
    SwapManager();
    
    bool is_active;
    uint64_t current_size;
    uint64_t page_count;

    // Helper to parse "512M" -> bytes
    uint64_t parse_size(const char* str);
};

#endif