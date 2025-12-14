#ifndef PS2_KBD_H
#define PS2_KBD_H

// Initialize the PS/2 controller and unmask IRQ 1
void ps2_init();

// The function called by the ISR to process the scancode
void ps2_irq_callback();

#endif