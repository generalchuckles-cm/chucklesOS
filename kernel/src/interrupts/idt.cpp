#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "../render.h"
#include "../globals.h" 
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../drv/usb/xhci.h" 
#include "../drv/ps2/ps2_mouse.h"
#include "../drv/net/e1000.h"
#include "../drv/ps2/ps2_kbd.h"

struct IDTEntry {
    uint16_t offset_1; 
    uint16_t selector; 
    uint8_t  ist;      
    uint8_t  type_attr;
    uint16_t offset_2; 
    uint32_t offset_3; 
    uint32_t zero;     
} __attribute__((packed));

struct IDTPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static IDTEntry idt[256];
static IDTPtr idtr;

extern "C" void isr0();  extern "C" void isr1();  extern "C" void isr2();
extern "C" void isr3();  extern "C" void isr4();  extern "C" void isr5();
extern "C" void isr6();  extern "C" void isr7();  extern "C" void isr8();
extern "C" void isr9();  extern "C" void isr10(); extern "C" void isr11();
extern "C" void isr12(); extern "C" void isr13(); extern "C" void isr14();
extern "C" void isr15(); extern "C" void isr16(); extern "C" void isr17();
extern "C" void isr18(); extern "C" void isr19(); extern "C" void isr20();
extern "C" void isr21(); extern "C" void isr22(); extern "C" void isr23();
extern "C" void isr24(); extern "C" void isr25(); extern "C" void isr26();
extern "C" void isr27(); extern "C" void isr28(); extern "C" void isr29();
extern "C" void isr30(); extern "C" void isr31();

extern "C" void isr32(); extern "C" void isr33(); extern "C" void isr34();
extern "C" void isr35(); extern "C" void isr36(); extern "C" void isr37();
extern "C" void isr38(); extern "C" void isr39(); extern "C" void isr40();
extern "C" void isr41(); extern "C" void isr42(); extern "C" void isr43();
extern "C" void isr44(); extern "C" void isr45(); extern "C" void isr46();
extern "C" void isr47();

extern "C" void isr128();

static void idt_set_gate(uint8_t num, void* base, uint16_t sel, uint8_t flags, uint8_t ist = 0) {
    uint64_t addr = (uint64_t)base;
    idt[num].offset_1 = addr & 0xFFFF;
    idt[num].offset_2 = (addr >> 16) & 0xFFFF;
    idt[num].offset_3 = (addr >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel;
    idt[num].ist = ist; 
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

void idt_init() {
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;
    memset(&idt, 0, sizeof(idt));

    uint16_t kernel_cs;
    asm volatile("mov %%cs, %0" : "=r"(kernel_cs));

    for (int i = 0; i < 32; i++) {
        uint8_t ist = 0;
        if (i == 8 || i == 14) ist = 1; // Use Panic Stack for Double/Page Faults

        void* handler = nullptr;
        switch(i) {
            case 0: handler = (void*)isr0; break;
            case 1: handler = (void*)isr1; break;
            case 2: handler = (void*)isr2; break;
            case 3: handler = (void*)isr3; break;
            case 4: handler = (void*)isr4; break;
            case 5: handler = (void*)isr5; break;
            case 6: handler = (void*)isr6; break;
            case 7: handler = (void*)isr7; break;
            case 8: handler = (void*)isr8; break;
            case 9: handler = (void*)isr9; break;
            case 10: handler = (void*)isr10; break;
            case 11: handler = (void*)isr11; break;
            case 12: handler = (void*)isr12; break;
            case 13: handler = (void*)isr13; break;
            case 14: handler = (void*)isr14; break;
            case 15: handler = (void*)isr15; break;
            case 16: handler = (void*)isr16; break;
            case 17: handler = (void*)isr17; break;
            case 18: handler = (void*)isr18; break;
            case 19: handler = (void*)isr19; break;
            case 20: handler = (void*)isr20; break;
            case 21: handler = (void*)isr21; break;
            case 22: handler = (void*)isr22; break;
            case 23: handler = (void*)isr23; break;
            case 24: handler = (void*)isr24; break;
            case 25: handler = (void*)isr25; break;
            case 26: handler = (void*)isr26; break;
            case 27: handler = (void*)isr27; break;
            case 28: handler = (void*)isr28; break;
            case 29: handler = (void*)isr29; break;
            case 30: handler = (void*)isr30; break;
            case 31: handler = (void*)isr31; break;
        }
        idt_set_gate(i, handler, kernel_cs, 0x8E, ist);
    }

    for(int i=0; i<16; i++) {
        void* handler = nullptr;
        switch(i) {
            case 0: handler = (void*)isr32; break;
            case 1: handler = (void*)isr33; break;
            case 2: handler = (void*)isr34; break;
            case 3: handler = (void*)isr35; break;
            case 4: handler = (void*)isr36; break;
            case 5: handler = (void*)isr37; break;
            case 6: handler = (void*)isr38; break;
            case 7: handler = (void*)isr39; break;
            case 8: handler = (void*)isr40; break;
            case 9: handler = (void*)isr41; break;
            case 10: handler = (void*)isr42; break;
            case 11: handler = (void*)isr43; break;
            case 12: handler = (void*)isr44; break;
            case 13: handler = (void*)isr45; break;
            case 14: handler = (void*)isr46; break;
            case 15: handler = (void*)isr47; break;
        }
        idt_set_gate(32+i, handler, kernel_cs, 0x8E, 0);
    }

    // SYSCALL: 0xEE = Present, Ring 3, 32-bit Gate
    idt_set_gate(0x80, (void*)isr128, kernel_cs, 0xEE, 0);

    asm volatile ("lidt %0" : : "m"(idtr));
    printf("IDT: Initialized.\n");
}

static const char* exception_messages[] = {
    "Division By Zero", "Debug", "NMI", "Breakpoint", "Overflow", "Bound Range", "Invalid Opcode", "No Device",
    "Double Fault", "Coprocessor Seg", "Bad TSS", "Segment Not Present", "Stack Fault", "GP Fault", "Page Fault", "Unknown",
    "x87 Fault", "Alignment Check", "Machine Check", "SIMD Fault", "Virtualization", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Security", "Reserved"
};

extern "C" void exception_handler(InterruptFrame* frame) {
    asm volatile("cli");
    
    if (g_renderer) {
        g_renderer->clear(0x0000AA); 
        
        char buf[256];
        int y = 50;
        int x = 50;
        int scale = 2; 
        
        g_renderer->drawString(x, y, "*** CHUCKLES OS KERNEL PANIC ***", 0xFFFFFF, scale);
        y += 30;

        if (frame->int_number < 32) {
            sprintf(buf, "Exception: %s (Vector 0x%x)", exception_messages[frame->int_number], frame->int_number);
        } else {
            sprintf(buf, "Exception: Unknown (Vector 0x%x)", frame->int_number);
        }
        g_renderer->drawString(x, y, buf, 0xFFFFFF, scale);
        y += 30;

        sprintf(buf, "Error Code: 0x%x", frame->error_code);
        g_renderer->drawString(x, y, buf, 0xFFFFFF, scale);
        y += 30;

        sprintf(buf, "RIP: 0x%p  CS: 0x%x", frame->rip, frame->cs);
        g_renderer->drawString(x, y, buf, 0xFFFFFF, scale);
        y += 30;

        sprintf(buf, "RSP: 0x%p  SS: 0x%x", frame->rsp, frame->ss);
        g_renderer->drawString(x, y, buf, 0xFFFFFF, scale);
        y += 30;
        
        if (frame->int_number == 14) {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            sprintf(buf, "CR2 (Fault Address): 0x%p", cr2);
            g_renderer->drawString(x, y, buf, 0xFFFF00, scale);
            y += 30;
        }

        g_renderer->drawString(x, y+20, "System Halted. Please reboot.", 0xCCCCCC, scale);
    } 

    while(1) asm volatile("hlt");
}

extern "C" void irq_handler(InterruptFrame* frame) {
    uint8_t irq = frame->int_number - 32;

    if (g_sniffer_mode) sniffer_log_irq(irq, frame->rip);
    
    if (irq == 1) ps2_irq_callback();
    else if (irq == 12) ps2_mouse_irq_callback();
    
    if (irq == E1000Driver::getInstance().get_irq()) {
        E1000Driver::getInstance().handle_interrupt();
    }
    
    if (irq >= 10 && irq <= 15) XhciDriver::getInstance().poll_events();

    pic_eoi(irq);
}