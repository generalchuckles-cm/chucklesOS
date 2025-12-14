#ifndef SSE_H
#define SSE_H

#include <cstdint>

// We use inline assembly because we don't have the standard <emmintrin.h>
// 128-bit integer store (moves 16 bytes from register to memory)
static inline void _sse_store_128(void* addr, uint32_t val) {
    // Create a 128-bit value where all 4 dwords are 'val'
    // We can't use standard vector types easily without standard lib support
    // so we use asm.
    asm volatile (
        "movd %0, %%xmm0\n\t"       // Move val to bottom of xmm0
        "pshufd $0, %%xmm0, %%xmm0\n\t" // Broadcast val to all 4 slots
        "movdqa %%xmm0, (%1)"       // Store 16 bytes to addr (must be aligned 16)
        : 
        : "r"(val), "r"(addr)
        : "xmm0", "memory"
    );
}

// Unaligned store (slower, but safe for unaligned addresses)
static inline void _sse_storeu_128(void* addr, uint32_t val) {
    asm volatile (
        "movd %0, %%xmm0\n\t"
        "pshufd $0, %%xmm0, %%xmm0\n\t"
        "movdqu %%xmm0, (%1)"
        : 
        : "r"(val), "r"(addr)
        : "xmm0", "memory"
    );
}

#endif