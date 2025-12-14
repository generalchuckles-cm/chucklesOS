#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

// Initializes the PS/2 Mouse, enables it, and unmasks IRQ 12
void ps2_mouse_init();

// Called by ISR 44 (IRQ 12) to process mouse packets
void ps2_mouse_irq_callback();

#endif