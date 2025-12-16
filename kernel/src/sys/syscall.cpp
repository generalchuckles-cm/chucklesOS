#include "syscalls.h"
#include "../cppstd/stdio.h"
#include "../input.h"
#include "../render.h"
#include "../globals.h"
#include "../timer.h"
#include "../io.h"
#include "../memory/heap.h"
#include "../fs/fat32.h"
#include "../drv/storage/ahci.h"

// Defined in interrupts.asm
extern "C" void restore_kernel_stack();
extern int g_sata_port;

extern "C" void cpp_syscall_handler(uint64_t syscall_id, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    switch (syscall_id) {
        case SYS_EXIT:
            // Return to kernel context saved in interrupts.asm
            sti(); 
            restore_kernel_stack();
            break;

        case SYS_PRINT:
            // arg1 = string pointer (Virtual Address in User Space)
            // Since we map user space into the current page table (or identity map),
            // and we are in Ring 0, we can access this pointer directly.
            if (arg1) printf((const char*)arg1, arg2, arg3);
            break;

        case SYS_GETCH:
            {
                sti();
                char c = input_get_char();
                asm volatile("mov %0, %%rax" :: "r"((uint64_t)c));
            }
            break;

        case SYS_DRAW_PIXEL:
            if (g_renderer) g_renderer->putPixel(arg1, arg2, arg3);
            break;

        case SYS_SLEEP:
            sti(); 
            sleep_ms(arg1);
            break;
            
        case SYS_DRAW_RECT:
            if (g_renderer) g_renderer->drawRect(arg1, arg2, arg3, arg4, arg5);
            break;

        case SYS_MALLOC:
            // arg1 = size. Return pointer in RAX.
            {
                void* ptr = malloc(arg1);
                asm volatile("mov %0, %%rax" :: "r"((uint64_t)ptr));
            }
            break;

        case SYS_FREE:
            // arg1 = ptr
            free((void*)arg1);
            break;

        case SYS_DISK_READ:
            // arg1=lba, arg2=count, arg3=buffer
            if (g_sata_port != -1) {
                AhciDriver::getInstance().read(g_sata_port, arg1, arg2, (void*)arg3);
            }
            break;

        case SYS_DISK_WRITE:
            // arg1=lba, arg2=count, arg3=buffer
            if (g_sata_port != -1) {
                AhciDriver::getInstance().write(g_sata_port, arg1, arg2, (void*)arg3);
            }
            break;

        default:
            printf("SYSCALL: Unknown ID %d\n", (int)syscall_id);
            break;
    }
}