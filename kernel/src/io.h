#ifndef IO_H
#define IO_H

#include <cstdint>

// Write a byte to a port
static inline void outb(std::uint16_t port, std::uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

// Read a byte from a port
static inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

// --- 32-bit I/O for PCI ---
static inline void outb_32(std::uint16_t port, std::uint32_t val) {
    asm volatile ( "outl %0, %1" : : "a"(val), "Nd"(port) );
}

static inline std::uint32_t inb_32(std::uint16_t port) {
    std::uint32_t ret;
    asm volatile ( "inl %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

// Wait a tiny bit (for hardware to catch up)
static inline void io_wait(void) {
    outb(0x80, 0);
}

// CPU Control
static inline void cli() { asm volatile("cli"); }
static inline void sti() { asm volatile("sti"); }
static inline void hlt() { asm volatile("hlt"); }

#endif