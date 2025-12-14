#ifndef GDT_H
#define GDT_H

#include <cstdint>

struct TSS {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1; // PANIC STACK
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct GDTDescriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Initialize GDT + TSS to catch stack corruptions
void gdt_init();

#endif