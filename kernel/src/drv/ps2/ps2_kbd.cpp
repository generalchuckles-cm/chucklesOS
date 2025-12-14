#include "ps2_kbd.h"
#include "../../io.h"
#include "../../input.h"
#include "../../interrupts/pic.h"
#include "../../cppstd/stdio.h"

void ps2_init() {
    printf("PS/2: Initializing Interrupts...\n");
    
    // 1. Drain the Output Buffer (discard garbage)
    while(inb(0x64) & 1) {
        inb(0x60);
    }
    
    // 2. Unmask IRQ 1 on the Master PIC
    pic_unmask(1);
}

void ps2_irq_callback() {
    // Read the scancode from Port 0x60
    uint8_t scancode = inb(0x60);
    
    // Send it to the shared input processing logic
    input_process_scancode(scancode);
}