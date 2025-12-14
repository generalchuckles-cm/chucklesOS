#include "terminal.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../cppstd/stdlib.h"
#include "../globals.h"
#include "../timer.h"
#include "../pci/lspci.h"
#include "../memory/heap.h"
#include "../memory/swp.h"
#include "../memory/pmm.h"
#include "../fs/fat32.h"
#include "../io.h"
#include "../loader/raw_loader.h" // CHANGED
#include "../drv/usb/xhci.h"
#include "../drv/storage/ahci.h"
#include "../drv/net/e1000.h"
#include "../input.h" 
#include "../net/network.h" 
#include "../net/tcp.h"
#include "../sys/chuckles_daemon.h"

#include "../stress/stress.h"
#include "../dvd.h"
#include "../3dengine/engine.h"
#include "../sound/midi_player.h"
#include "../apps/nes.h"
#include "../apps/compiler.h"
#include "../apps/text_editor.h"
#include "../browse/browse.h"
#include "../apps/cpl_compiler.h"

extern int g_sata_port;

void TerminalApp::on_init(Window* win) {
    my_window = win;
    input_index = 0;
    memset(input_buffer, 0, sizeof(input_buffer));
    g_console = win->console;
    win->renderer->clear(0x000000);
    win->console->setColor(0xFFFFFF, 0x000000);
    win->console->print("ChucklesOS Shell (Kernel Mode CPL)\n$ ");
}

void TerminalApp::on_draw() {}

void TerminalApp::on_input(char c) {
    g_console = my_window->console;
    if (c == '\n') {
        g_console->putChar('\n');
        if (input_index > 0) execute_command();
        memset(input_buffer, 0, sizeof(input_buffer));
        input_index = 0;
        g_console->print("$ ");
    } else if (c == '\b') {
        if (input_index > 0) {
            input_index--;
            input_buffer[input_index] = 0;
            g_console->putChar('\b');
        }
    } else if (input_index < 255) {
        input_buffer[input_index++] = c;
        input_buffer[input_index] = 0;
        g_console->putChar(c);
    }
}

void TerminalApp::execute_command() {
    char* argv[32];
    int argc = 0;
    bool in_token = false;
    
    for (int i = 0; input_buffer[i] != 0 && argc < 32; i++) {
        if (input_buffer[i] == ' ') { 
            input_buffer[i] = 0; in_token = false; 
        } else if (!in_token) { 
            argv[argc++] = &input_buffer[i]; in_token = true; 
        }
    }
    
    if (argc == 0) return;

    // --- COMMANDS ---
    if (strcmp(argv[0], "cpl") == 0) {
        if(argc > 2) cpl_compile(argv[1], argv[2]);
        else printf("Usage: cpl <source.cpl> <output.bin>\n");
    }
    else if (strcmp(argv[0], "exec") == 0) {
        if(argc>1) RawLoader::load_and_run(argv[1], argc - 1, &argv[1]);
    }
    else if (strcmp(argv[0], "ls") == 0) Fat32::getInstance().ls();
    else if (strcmp(argv[0], "help") == 0) {
        printf("Commands: help, ls, edit, cpl, exec, reboot\n");
    }
    else if (strcmp(argv[0], "reboot") == 0) outb(0x64, 0xFE);
    else if (strcmp(argv[0], "edit") == 0) {
        if(argc>1) run_text_editor(argv[1]);
    }
    else if (strcmp(argv[0], "clear") == 0) my_window->renderer->clear(0);
    
    // Re-include previous commands for completeness
    else if (strcmp(argv[0], "sysinfo") == 0) {
        printf("ChucklesOS Kernel Mode\n");
        printf("RAM: %d MB Used\n", (int)(pmm_get_used_memory()/1024/1024));
    }
    else if (strcmp(argv[0], "browse") == 0) {
        if (argc > 1) {
            BrowserApp* app = new BrowserApp();
            Window* win = new Window(150, 150, 800, 600, "ChucklesBrowse", app);
            WindowManager::getInstance().add_window(win);
            app->navigate(argv[1]);
        }
    }
    else {
        printf("Unknown: %s\n", argv[0]);
    }
}