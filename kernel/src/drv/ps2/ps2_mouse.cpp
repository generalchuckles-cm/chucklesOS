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
    if (type == 0) { 
        while (timeout--) if ((inb(MOUSE_PORT_STATUS) & 1) == 1) return;
    } else { 
        while (timeout--) if ((inb(MOUSE_PORT_STATUS) & 2) == 0) return;
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
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xA8);
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x20);
    uint8_t status = mouse_read();
    status |= 2; 
    status &= ~0x20; 
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, status);
    mouse_write(0xF6);
    mouse_read(); 
    mouse_write(0xF4);
    mouse_read(); 
    pic_unmask(2);
    pic_unmask(12);
}

void ps2_mouse_irq_callback() {
    uint8_t status = inb(MOUSE_PORT_CMD);
    if (!(status & 0x20)) return; 
    uint8_t b = inb(MOUSE_PORT_DATA);
    if (mouse_cycle == 0 && !(b & 0x08)) return; 
    
    mouse_packet[mouse_cycle] = b;
    mouse_cycle++;
    
    if (mouse_cycle == 3) {
        mouse_cycle = 0;
        
        // Bounds checking is deliberately deferred to the WindowManager 
        // to support dynamically scaled virtual resolution spaces.
        g_mouse_x += (char)mouse_packet[1];
        g_mouse_y -= (char)mouse_packet[2];
        
        g_mouse_left   = (mouse_packet[0] & 0x01);
        g_mouse_right  = (mouse_packet[0] & 0x02);
        g_mouse_middle = (mouse_packet[0] & 0x04);
    }
}
