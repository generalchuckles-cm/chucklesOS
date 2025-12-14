#ifndef TIMER_H
#define TIMER_H

#include <cstdint>

// Waits for a specific number of CPU clock cycles (ticks).
void sleep_ticks(uint64_t ticks);

// New: Waits for a specific number of milliseconds (CPU independent)
void sleep_ms(uint64_t ms);

// Serialized RDTSC (waits for instructions to retire before reading)
uint64_t rdtsc_serialized();

// Detects CPU Frequency using CPUID Leaf 0x16 (or 0x15).
uint64_t get_cpu_frequency();

#endif