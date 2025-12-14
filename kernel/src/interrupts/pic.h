#ifndef PIC_H
#define PIC_H

#include <cstdint>

// Initialize the 8259 PIC and remap IRQs to 0x20-0x2F
void pic_init();

// Tell PIC we are done handling an interrupt
void pic_eoi(uint8_t irq);

// Unmask a specific IRQ line
void pic_unmask(uint8_t irq);

#endif