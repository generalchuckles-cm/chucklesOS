#include "timer.h"
#include "input.h"

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    asm volatile ("cpuid" 
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) 
        : "a"(leaf)
        : "memory"
    );
}

uint64_t rdtsc_serialized() {
    uint32_t lo, hi;
    // Clear EAX to ensure leaf 0 is called for serialization
    // Fixed clobber list (no % prefix for registers in clobber)
    asm volatile (
        "xor %%eax, %%eax\n\t"
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        :
        : "rbx", "rcx" 
    );
    return ((uint64_t)hi << 32) | lo;
}

void sleep_ticks(uint64_t ticks) {
    uint64_t start_ticks = rdtsc_serialized();
    while (rdtsc_serialized() - start_ticks < ticks) {
        check_input_hooks();
        asm volatile ("pause");
    }
}

uint64_t get_cpu_frequency() {
    static uint64_t cached_freq = 0;
    if (cached_freq != 0) return cached_freq;

    uint32_t eax, ebx, ecx, edx;
    
    // 1. Check max CPUID leaf
    cpuid(0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;

    // 2. Try Leaf 0x16 (Processor Frequency Information)
    if (max_leaf >= 0x16) {
        cpuid(0x16, &eax, &ebx, &ecx, &edx);
        if (eax != 0) {
            cached_freq = (uint64_t)eax * 1000000;
            return cached_freq;
        }
    }

    // 3. Try Leaf 0x15 (Crystal Clock)
    if (max_leaf >= 0x15) {
        cpuid(0x15, &eax, &ebx, &ecx, &edx);
        if (ecx != 0 && eax != 0 && ebx != 0) {
            cached_freq = ((uint64_t)ecx * ebx) / eax;
            return cached_freq;
        }
    }

    // 4. Fallback (2 GHz)
    cached_freq = 2000000000;
    return cached_freq;
}

void sleep_ms(uint64_t ms) {
    uint64_t freq = get_cpu_frequency();
    if (freq == 0) freq = 2000000000; // Absolute safety fallback
    
    uint64_t ticks = (freq / 1000) * ms;
    sleep_ticks(ticks);
}