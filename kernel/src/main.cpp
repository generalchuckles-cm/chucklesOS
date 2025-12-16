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
#include "smp/smp.h" 
#include "sys/system_stats.h" 
#include "sys/raw_panic.h" 
#include "timer.h"

#include "drv/input/elan_touch.h"
#include "gui/window.h"
#include "apps/terminal.h"
#include "apps/display_settings.h"

Console* g_console = nullptr;
Renderer* g_renderer = nullptr;

DisplaySettings g_display_settings = { MODE_32BIT, 0, 0, false };

void (*g_ui_update_callback)() = nullptr;

volatile uint32_t* g_raw_fb_addr = nullptr;
volatile uint32_t g_raw_fb_width = 0;
volatile uint32_t g_raw_fb_height = 0;
volatile uint32_t g_raw_fb_pitch = 0;

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

static void kernel_ui_update_wrapper() {
    if (g_renderer) {
        // SAFETY: Disable I2C Polling to prevent hanging on emulators
        // ElanTouchpad::getInstance().poll();
        
        WindowManager::getInstance().update();
        WindowManager::getInstance().render(g_renderer);
    }
}

extern "C" void kmain() {
    enable_sse();
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) hcf();
    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++) __init_array[i]();
    if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1) hcf();

    limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    
    g_raw_fb_addr = (volatile uint32_t*)framebuffer->address;
    g_raw_fb_width = framebuffer->width;
    g_raw_fb_height = framebuffer->height;
    g_raw_fb_pitch = framebuffer->pitch;

    pmm_init(); 
    vmm_init(); 
    heap_init(); 
    gdt_init(); 
    idt_init();

    g_renderer = new Renderer(framebuffer, g_zap_font); 
    g_console = new Console(g_renderer);

    pic_init();
    ps2_init();       
    SystemStats::getInstance().service_ps2_active = true;
    
    ps2_mouse_init(); 
    // ElanTouchpad::getInstance().init(); // Disabled for stability

    asm volatile ("sti");
    g_using_interrupts = true; 

    smp_init();

    if (AhciDriver::getInstance().init()) {
        SystemStats::getInstance().service_ahci_active = true;
        g_sata_port = AhciDriver::getInstance().findFirstSataPort();
        if (g_sata_port != -1) Fat32::getInstance().init(g_sata_port);
    }
    
    WindowManager::getInstance().init(g_renderer->getWidth(), g_renderer->getHeight());
    g_ui_update_callback = kernel_ui_update_wrapper;
    
    TerminalApp* shell_app = new TerminalApp();
    Window* shell_win = new Window(100, 100, 600, 400, "Terminal", shell_app);
    WindowManager::getInstance().add_window(shell_win);

    printf("Kernel: Entering Main Loop...\n");
    
    // Initial Render
    WindowManager::getInstance().render(g_renderer);

    // Frame Limiter Variables
    uint64_t last_tick = rdtsc_serialized();
    uint64_t cpu_freq = get_cpu_frequency();
    if (cpu_freq == 0) cpu_freq = 2000000000; // Fallback
    uint64_t ticks_per_frame = cpu_freq / 60; // 60 FPS

    while (true) {
        // Non-blocking Input Check (PS/2 Buffer)
        check_input_hooks(); 
        SystemStats::getInstance().cpu_ticks[0]++;
        
        // Frame Limiter
        uint64_t now = rdtsc_serialized();
        if (now - last_tick >= ticks_per_frame) {
            last_tick = now;
            // Force UI update at 60Hz
            kernel_ui_update_wrapper();
        } else {
            asm volatile("pause");
        }
    }
}   