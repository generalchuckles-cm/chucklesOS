#include "ps2_mouse.h"
#include "../../io.h"
#include "../../input.h"
#include "../../interrupts/pic.h"
#include "../../cppstd/stdio.h"
#include "../../globals.h"

#define MOUSE_PORT_DATA    0x60
#define MOUSE_PORT_STATUS  0x64
#define MOUSE_PORT_CMD     0x64

static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[4]; 

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) { // Wait for Data to Read
        while (timeout--) {
            if ((inb(MOUSE_PORT_STATUS) & 1) == 1) return;
        }
    } else { // Wait for Signal to Write
        while (timeout--) {
            if ((inb(MOUSE_PORT_STATUS) & 2) == 0) return;
        }
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, write);
}

static uint8_t mouse_read() {
    mouse_wait(0);
    return inb(MOUSE_PORT_DATA);
}

void ps2_mouse_init() {
    printf("PS/2: Initializing Mouse...\n");

    // 1. Enable Auxiliary Device
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xA8);

    // 2. Enable Interrupts
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x20);
    uint8_t status = mouse_read();
    status |= 2; // Enable IRQ 12
    status &= ~0x20; // Disable Mouse Clock
    
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, status);

    // 3. Set Defaults
    mouse_write(0xF6);
    mouse_read(); // ACK

    // 4. Enable Data Reporting
    mouse_write(0xF4);
    mouse_read(); // ACK

    // 5. Unmask IRQ 12 (Mouse) AND IRQ 2 (Cascade)
    // IMPORTANT: IRQ 12 is on the Slave PIC. We must enable the cascade (IRQ 2)
    // on the Master PIC, or we will never hear from the mouse.
    pic_unmask(2);
    pic_unmask(12);
    
    if (g_renderer) {
        g_mouse_x = g_renderer->getWidth() / 2;
        g_mouse_y = g_renderer->getHeight() / 2;
    }
}

void ps2_mouse_irq_callback() {
    uint8_t status = inb(MOUSE_PORT_CMD);
    if (!(status & 0x20)) return; 

    uint8_t b = inb(MOUSE_PORT_DATA);

    if (mouse_cycle == 0 && !(b & 0x08)) return; // Sync bit missing

    mouse_packet[mouse_cycle] = b;
    mouse_cycle++;

    if (mouse_cycle == 3) {
        mouse_cycle = 0;

        uint8_t state = mouse_packet[0];
        
        // Cast to char for sign extension
        int rel_x = (char)mouse_packet[1];
        int rel_y = (char)mouse_packet[2];

        // PS/2 Y is inverted relative to screen coordinates
        g_mouse_x += rel_x;
        g_mouse_y -= rel_y;

        if (g_renderer) {
            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_x >= (int)g_renderer->getWidth()) g_mouse_x = g_renderer->getWidth() - 1;
            
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_y >= (int)g_renderer->getHeight()) g_mouse_y = g_renderer->getHeight() - 1;
        }

        g_mouse_left   = (state & 0x01);
        g_mouse_right  = (state & 0x02);
        g_mouse_middle = (state & 0x04);
    }
}