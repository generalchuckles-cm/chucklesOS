#ifndef GLOBALS_H
#define GLOBALS_H

#include "console.h"
#include "render.h"
#include "display_defs.h" // Include new defs
#include <cstdint>

extern Console* g_console;
extern Renderer* g_renderer;

extern bool g_sniffer_mode;
extern volatile bool g_sniffer_dirty; 
extern uint64_t g_irq_counts[16];

extern void (*g_ui_update_callback)();

void sniffer_log_irq(int irq, uint64_t rip);
void sniffer_log_custom(const char* message);

#endif