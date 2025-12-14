#include <cstdint>
#include <cstddef>
#include <limine.h>
#include "render.h"
#include "console.h"
#include "font.h" 
#include "io.h"
#include "globals.h"
#include "cppstd/stdio.h"
#include "cppstd/string.h"
#include "input.h" 
#include "pci/pci.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "drv/gpu/intel_gpu.h" 
#include "interrupts/idt.h"
#include "interrupts/gdt.h" 
#include "interrupts/pic.h"
#include "drv/ps2/ps2_kbd.h"
#include "drv/ps2/ps2_mouse.h"
#include "drv/usb/xhci.h" 
#include "drv/storage/ahci.h"
#include "fs/fat32.h"

// NEW HEADERS
#include "drv/input/elan_touch.h"

// Window Manager Includes
#include "gui/window.h"
#include "apps/terminal.h"

Console* g_console = nullptr;
Renderer* g_renderer = nullptr;

bool g_sniffer_mode = false;
volatile bool g_sniffer_dirty = false;
uint64_t g_irq_counts[16] = {0};
const char* g_irq_names[16] = { "Timer", "Kbd", "Casc", "COM2", "COM1", "LPT2", "Flop", "LPT1", "CMOS", "Free", "Free", "Free", "Mouse", "FPU", "ATA1", "ATA2" };
int g_xhci_irq_line = -1;
int g_sata_port = -1;

void sniffer_log_irq(int, uint64_t) {}
void sniffer_log_custom(const char*) {}

namespace {
    __attribute__((used, section(".limine_requests")))
    volatile std::uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(2);

    __attribute__((used, section(".limine_requests")))
    volatile struct limine_framebuffer_request framebuffer_request = { 
        .id = LIMINE_FRAMEBUFFER_REQUEST_ID, 
        .revision = 0,
        .response = nullptr
    };
    
    __attribute__((used, section(".limine_requests")))
    volatile struct limine_executable_cmdline_request cmdline_request = { 
        .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, 
        .revision = 0,
        .response = nullptr
    };
    
    __attribute__((used, section(".limine_requests_start")))
    volatile std::uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;
    
    __attribute__((used, section(".limine_requests_end")))
    volatile std::uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;
}

namespace { void hcf() { for (;;) { asm ("hlt"); } } }

extern "C" {
    int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
    void __cxa_pure_virtual() { hcf(); }
    void *__dso_handle;
    int __cxa_guard_acquire(int64_t *guard) { volatile char *i = (volatile char *)guard; return *i == 0; }
    void __cxa_guard_release(int64_t *guard) { volatile char *i = (volatile char *)guard; *i = 1; }
}

extern void (*__init_array[])();
extern void (*__init_array_end[])();

static void enable_sse() {
    uint64_t cr0, cr4;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); cr0 |= (1 << 1);  
    asm volatile ("mov %0, %%cr0" :: "r"(cr0));
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (3 << 9);  
    asm volatile ("mov %0, %%cr4" :: "r"(cr4));
}

extern "C" void kmain() {
    enable_sse();
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) hcf();
    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++) __init_array[i]();
    if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1) hcf();

    limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    
    pmm_init(); 
    vmm_init(); 
    heap_init(); 
    gdt_init(); 
    idt_init();

    // Global Renderer (for WM to use)
    Renderer renderer(framebuffer, g_zap_font); 
    g_renderer = &renderer;

    // Boot Console (temporary)
    Console console(&renderer);
    g_console = &console;

    // --- Hardware Init ---
    pic_init();
    ps2_init();       // Keyboard
    ps2_mouse_init(); // PS/2 Mouse
    
    // Initialize I2C and Elan Touchpad
    ElanTouchpad::getInstance().init();

    asm volatile ("sti");
    g_using_interrupts = true; 

    if (AhciDriver::getInstance().init()) {
        g_sata_port = AhciDriver::getInstance().findFirstSataPort();
        if (g_sata_port != -1) Fat32::getInstance().init(g_sata_port);
    }
    
    // --- Window Manager Start ---
    WindowManager::getInstance().init(renderer.getWidth(), renderer.getHeight());
    
    // Create the first Terminal Window
    TerminalApp* shell_app = new TerminalApp();
    Window* shell_win = new Window(100, 100, 600, 400, "Terminal", shell_app);
    WindowManager::getInstance().add_window(shell_win);

    // Main Loop
    while (true) {
        check_input_hooks(); 
        
        // Poll Touchpad
        ElanTouchpad::getInstance().poll();
        
        WindowManager::getInstance().update();
        WindowManager::getInstance().render(g_renderer);
    }
}