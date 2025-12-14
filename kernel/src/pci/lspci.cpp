#include "lspci.h"
#include "pci.h"
#include "../io.h" 
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../memory/heap.h"
#include "../input.h"
#include "../globals.h"
#include "../timer.h"

// --- Helpers to decode Class/Subclass ---

static const char* get_class_name(uint8_t class_id) {
    switch (class_id) {
        case 0x00: return "Legacy";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Comm. Cntrlr";
        case 0x08: return "System Periph";
        case 0x0C: return "Serial Bus";
        case 0x11: return "Signal Proc";
        default:   return "Unknown";
    }
}

static const char* get_subclass_name(uint8_t class_id, uint8_t subclass_id) {
    if (class_id == 0x01) { // Storage
        if (subclass_id == 0x01) return "IDE";
        if (subclass_id == 0x06) return "SATA";
        if (subclass_id == 0x08) return "NVMe";
    }
    if (class_id == 0x03) { // Display
        if (subclass_id == 0x00) return "VGA";
        if (subclass_id == 0x80) return "Other";
    }
    if (class_id == 0x04) { // Multimedia
        if (subclass_id == 0x00) return "Video";
        if (subclass_id == 0x01) return "Audio";
        if (subclass_id == 0x03) return "HDA (High Def Audio)";
    }
    if (class_id == 0x06) { // Bridge
        if (subclass_id == 0x00) return "Host/PCI";
        if (subclass_id == 0x01) return "PCI/ISA";
        if (subclass_id == 0x04) return "PCI/PCI";
    }
    if (class_id == 0x0C) { // Serial
        if (subclass_id == 0x03) return "USB";
        if (subclass_id == 0x05) return "SMBus";
    }
    return "";
}

// Simple blocking wait for a key using the new Input API
static char wait_key() {
    return input_get_char();
}

void lspci_run_detailed(bool autoscroll) {
    printf("\nscanning pci bus...\n");

    // 1. Gather all devices first
    const int MAX_DEVICES = 64;
    PCIDevice* devices = new PCIDevice[MAX_DEVICES];
    
    // SAFETY CHECK: Ensure allocation succeeded
    if (!devices) {
        printf("LSPCI Error: Out of memory (Heap Full).\n");
        return;
    }

    int count = 0;

    // Manually scan bus 0-255
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            // Read Vendor (Offset 0x00)
            uint32_t addr = (0x80000000) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11);
            outb_32(0xCF8, addr);
            uint16_t vendor = (uint16_t)(inb_32(0xCFC) & 0xFFFF);

            if (vendor != 0xFFFF) {
                if (count >= MAX_DEVICES) break;

                PCIDevice& dev = devices[count++];
                dev.bus = bus;
                dev.slot = slot;
                dev.function = 0;
                dev.vendor_id = vendor;
                
                // Read Device ID (Offset 0x02) - upper 16 bits of reg 0
                dev.device_id = (uint16_t)(inb_32(0xCFC) >> 16);

                // Read Class/Subclass (Offset 0x08)
                // Register 0x08 contains: RevID, ProgIF, Subclass, Class
                outb_32(0xCF8, addr | 0x08);
                uint32_t class_reg = inb_32(0xCFC);
                dev.class_id = (class_reg >> 24) & 0xFF;
                dev.subclass_id = (class_reg >> 16) & 0xFF;
                dev.prog_if = (class_reg >> 8) & 0xFF;

                // Read BAR0 (Offset 0x10)
                outb_32(0xCF8, addr | 0x10);
                dev.bar0 = inb_32(0xCFC);
            }
        }
    }

    printf("found %d devices.\n", count);
    
    int printed_lines = 0;
    
    // Clear screen for fresh start
    if (g_renderer) g_renderer->clear(0x000000);
    if (g_console) g_console->setColor(0x00FFFF, 0x000000); // Cyan text

    printf("--- PCI BUS LISTING ---\n");
    printf("BUS:SLOT  VEN:DEV   CLASS (SUB)      DESCRIPTION\n");
    printf("------------------------------------------------\n");
    printed_lines += 3;

    for (int i = 0; i < count; i++) {
        PCIDevice& d = devices[i];
        
        const char* cls_name = get_class_name(d.class_id);
        const char* sub_name = get_subclass_name(d.class_id, d.subclass_id);

        printf("%02x:%02x     %04x:%04x %02x:%02x %s - %s\n", 
            d.bus, d.slot, d.vendor_id, d.device_id, d.class_id, d.subclass_id, 
            cls_name, sub_name);

        printed_lines++;

        // Detailed view extra line for interesting devices
        if (d.class_id == 0x04 && d.subclass_id == 0x03) {
            printf("      -> INTEL HDA CANDIDATE (BAR0: 0x%x)\n", d.bar0);
            printed_lines++;
        }
        if (d.class_id == 0x03) {
             printf("      -> GPU (BAR0: 0x%x)\n", d.bar0);
             printed_lines++;
        }
        // XHCI / USB
        if (d.class_id == 0x0C && d.subclass_id == 0x03) {
             printf("      -> xHCI USB (BAR0: 0x%x)\n", d.bar0);
             printed_lines++;
        }

        if (printed_lines >= 20) {
            if (autoscroll) {
                printf("-- Auto-scrolling in 30s (ESC to abort) --");
                
                uint64_t freq = get_cpu_frequency();
                uint64_t end_tick = rdtsc_serialized() + (freq * 30);
                
                bool stop_scroll = false;
                
                // Poll for ESC while waiting
                while (rdtsc_serialized() < end_tick) {
                    check_input_hooks();
                    char c = input_check_char();
                    if (c == 27) {
                        stop_scroll = true;
                        break;
                    }
                    asm volatile("pause");
                }
                
                // Clear the status line
                printf("\r                                          \r");
                
                if (stop_scroll) break;
                printed_lines = 0;

            } else {
                printf("-- PRESS SPACE FOR MORE, ESC TO QUIT --");
                char c = wait_key();
                
                // Clear the prompt line
                printf("\r                                       \r");
                
                if (c == 27) break; // ESC
                printed_lines = 0;
            }
        }
    }

    printf("\n--- END OF LIST ---\n");
    if (g_console) g_console->setColor(0xFFFFFF, 0x000000); // Restore White
    
    delete[] devices;
}