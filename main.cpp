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
#include "interrupts/idt.h"
#include "interrupts/gdt.h" 
#include "interrupts/pic.h"
#include "drv/ps2/ps2_kbd.h"
#include "drv/ps2/ps2_mouse.h"
#include "drv/storage/ahci.h"
#include "fs/fat32.h"
#include "smp/smp.h" 
#include "sys/system_stats.h" 
#include "sys/raw_panic.h" 
#include "timer.h"

#include "gui/window.h"
#include "apps/terminal.h"
#include "apps/display_settings.h"

Console* g_console = nullptr;
Renderer* g_renderer = nullptr;

// Safe default set to 1600x900
DisplaySettings g_display_settings = { MODE_32BIT, 1600, 900, false };
HardwareDisplayInfo g_hardware_display = { 0, {}, 0, 0 };

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
        .revision = 1, // Request revision 1 to receive hardware video modes
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

    // Cache Native and Hardware Modes
    g_hardware_display.native_width = framebuffer->width;
    g_hardware_display.native_height = framebuffer->height;
    g_hardware_display.mode_count = 0;

    if (framebuffer_request.response->revision >= 1 && framebuffer->modes != nullptr) {
        for (uint64_t i = 0; i < framebuffer->mode_count && g_hardware_display.mode_count < MAX_HARDWARE_MODES; i++) {
            struct limine_video_mode* m = framebuffer->modes[i];
            if (!m) continue;
            bool dup = false;
            for (uint32_t j = 0; j < g_hardware_display.mode_count; j++) {
                if (g_hardware_display.modes[j].width == m->width &&
                    g_hardware_display.modes[j].height == m->height) {
                    dup = true;
                    break;
                }
            }
            if (!dup && m->width >= 640 && m->height >= 480) {
                g_hardware_display.modes[g_hardware_display.mode_count].width = (uint32_t)m->width;
                g_hardware_display.modes[g_hardware_display.mode_count].height = (uint32_t)m->height;
                g_hardware_display.modes[g_hardware_display.mode_count].bpp = m->bpp;
                g_hardware_display.mode_count++;
            }
        }
    }

    // Fallback common modes if bootloader provides none
    if (g_hardware_display.mode_count == 0) {
        const uint32_t def_res[][2] = {
            {1600, 900}, {1920, 1080}, {1366, 768}, {1280, 720}, {1024, 768}, {800, 600}
        };
        for (int i = 0; i < 6 && g_hardware_display.mode_count < MAX_HARDWARE_MODES; i++) {
            g_hardware_display.modes[g_hardware_display.mode_count++] = {
                def_res[i][0], def_res[i][1], 32
            };
        }
    }

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

    uint64_t last_tick = rdtsc_serialized();
    uint64_t cpu_freq = get_cpu_frequency();
    if (cpu_freq == 0) cpu_freq = 2000000000;
    uint64_t ticks_per_frame = cpu_freq / 60;

    while (true) {
        check_input_hooks(); 
        SystemStats::getInstance().cpu_ticks[0]++;
        
        uint64_t now = rdtsc_serialized();
        if (now - last_tick >= ticks_per_frame) {
            last_tick = now;
            kernel_ui_update_wrapper();
        } else {
            asm volatile("pause");
        }
    }
}
