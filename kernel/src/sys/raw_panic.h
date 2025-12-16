#ifndef RAW_PANIC_H
#define RAW_PANIC_H

#include <cstdint>
#include <limine.h>

// Global storage for raw framebuffer info (populated in main.cpp)
// Using volatile to ensure visibility across cores during crashes
extern volatile uint32_t* g_raw_fb_addr;
extern volatile uint32_t g_raw_fb_width;
extern volatile uint32_t g_raw_fb_height;
extern volatile uint32_t g_raw_fb_pitch;

// Simple 8x8 font bitmap for panic text (only 0-9, A-Z, space, !)
// Compressed/Simplified for raw panic usage
static const uint8_t g_panic_font[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x00, 0x18, 0x00, // !
    // ... Minimal set requires more data, but for now we draw color blocks for errors
};

// Emergency function to fill screen with a specific color
static inline void raw_panic_screen(uint32_t color) {
    if (!g_raw_fb_addr) return;
    
    // Simple loop, no SSE, no optimization, just raw writes
    for (uint32_t i = 0; i < g_raw_fb_width * g_raw_fb_height; i++) {
        g_raw_fb_addr[i] = color;
    }
}

// Emergency function to draw a square at coords (debug codes)
static inline void raw_panic_marker(int x, int y, uint32_t color) {
    if (!g_raw_fb_addr) return;
    for(int iy=0; iy<20; iy++) {
        for(int ix=0; ix<20; ix++) {
            uint32_t offset = (y + iy) * (g_raw_fb_pitch / 4) + (x + ix);
            g_raw_fb_addr[offset] = color;
        }
    }
}

#endif