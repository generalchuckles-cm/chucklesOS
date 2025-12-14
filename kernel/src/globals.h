#ifndef GLOBALS_H
#define GLOBALS_H

#include "console.h"
#include "render.h"
#include <cstdint>

// Defined in main.cpp, used by stdio.cpp and idt.cpp
extern Console* g_console;
extern Renderer* g_renderer;

// --- Sniffer Globals ---
extern bool g_sniffer_mode;
extern volatile bool g_sniffer_dirty; 
extern uint64_t g_irq_counts[16];

// Called by IDT to log an event to the bottom-screen buffer
void sniffer_log_irq(int irq, uint64_t rip);

// New: Called by drivers (like xHCI polling) to log specific messages
void sniffer_log_custom(const char* message);

#endif