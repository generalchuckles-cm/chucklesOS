#include "timer.h"
#include "input.h"
#include "io.h"

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    asm volatile ("cpuid" 
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) 
        : "a"(leaf)
        : "memory"
    );
}

uint64_t rdtsc_serialized() {
    uint32_t lo, hi;
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

// Hardware calibration of the CPU TSC using PIT Channel 2 (1,193,182 Hz oscillator)
static uint64_t calibrate_tsc_with_pit() {
    uint8_t gate = inb(0x61);

    // Channel 2, LSB/MSB, Mode 0 (Interrupt on Terminal Count), Binary
    outb(0x43, 0xB0);

    // Load count for ~50ms (1193182 * 0.05 = 59659)
    uint16_t pit_count = 59659;
    outb(0x42, (uint8_t)(pit_count & 0xFF));
    outb(0x42, (uint8_t)(pit_count >> 8));

    // Reset PIT Channel 2 gate (toggle bit 0 low then high)
    outb(0x61, (gate & 0xFC));
    outb(0x61, (gate & 0xFD) | 0x01);

    uint64_t start_tsc = rdtsc_serialized();

    // Poll until Output bit (bit 5 of Port 0x61) goes high (terminal count reached)
    uint32_t timeout = 10000000;
    while ((inb(0x61) & 0x20) == 0) {
        if (--timeout == 0) break;
    }

    uint64_t end_tsc = rdtsc_serialized();

    // Restore original Port 0x61 state
    outb(0x61, gate);

    if (timeout == 0 || end_tsc <= start_tsc) return 0;

    uint64_t tsc_delta = end_tsc - start_tsc;
    return (tsc_delta * 1193182ULL) / (uint64_t)pit_count;
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

    // 4. Measure via PIT Channel 2
    uint64_t pit_freq = calibrate_tsc_with_pit();
    if (pit_freq != 0) {
        cached_freq = pit_freq;
        return cached_freq;
    }

    // 5. Hardcoded Fallback (2 GHz)
    cached_freq = 2000000000;
    return cached_freq;
}

void sleep_ms(uint64_t ms) {
    uint64_t freq = get_cpu_frequency();
    if (freq == 0) freq = 2000000000; // Absolute safety fallback
    
    uint64_t ticks = (freq / 1000) * ms;
    sleep_ticks(ticks);
}