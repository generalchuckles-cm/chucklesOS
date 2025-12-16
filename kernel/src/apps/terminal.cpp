#include "terminal.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../cppstd/stdlib.h"
#include "../globals.h"
#include "../gui/window.h" 

// App Headers
#include "../dvd.h"
#include "../3dengine/engine.h"
#include "../apps/nes.h"
#include "../browse/browse.h"
#include "../apps/text_editor.h" 
#include "../apps/compiler.h"
#include "../apps/cpl_compiler.h"
#include "../apps/ccc.h"
#include "../apps/display_settings.h"

// Hardware Headers
#include "../pci/lspci.h"
#include "../memory/heap.h"
#include "../memory/swp.h"
#include "../memory/pmm.h"
#include "../fs/fat32.h"
#include "../io.h"
#include "../loader/raw_loader.h"
#include "../loader/high_loader.h"
#include "../drv/usb/xhci.h"
#include "../drv/storage/ahci.h"
#include "../drv/net/e1000.h"
#include "../net/network.h" 
#include "../sys/chuckles_daemon.h"

extern int g_sata_port;

void TerminalApp::on_init(Window* win) {
    my_window = win;
    input_index = 0;
    memset(input_buffer, 0, sizeof(input_buffer));
    
    g_console = win->console;
    
    win->renderer->clear(0x000000);
    win->console->setColor(0xFFFFFF, 0x000000);
    win->console->print("ChucklesOS Shell (Windowed)\n$ ");
}

void TerminalApp::on_draw() {}

void TerminalApp::on_input(char c) {
    g_console = my_window->console;
    
    if (c == '\n') {
        g_console->putChar('\n');
        if (input_index > 0) {
            execute_command();
        }
        memset(input_buffer, 0, sizeof(input_buffer));
        input_index = 0;
        g_console->print("$ ");
    } 
    else if (c == '\b') {
        if (input_index > 0) {
            input_index--;
            input_buffer[input_index] = 0;
            g_console->putChar('\b');
        }
    }
    else if (input_index < 255) {
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
            input_buffer[i] = 0; 
            in_token = false; 
        } else if (!in_token) { 
            argv[argc++] = &input_buffer[i]; 
            in_token = true; 
        }
    }
    
    if (argc == 0) return;

    // --- GUI APPS ---
    
    if (strcmp(argv[0], "dvd") == 0) {
        DVDApp* app = new DVDApp();
        Window* win = new Window(100, 100, 640, 480, "DVD Player", app);
        WindowManager::getInstance().add_window(win);
    }
    else if (strcmp(argv[0], "3drnd") == 0) {
        Engine3DApp* app = new Engine3DApp();
        Window* win = new Window(150, 150, 600, 600, "3D Engine", app);
        WindowManager::getInstance().add_window(win);
    }
    else if (strcmp(argv[0], "nes") == 0) {
        const char* rom = (argc > 1) ? argv[1] : NULL;
        NESApp* app = new NESApp(rom);
        Window* win = new Window(200, 200, 512, 480, "Nintendo", app); 
        WindowManager::getInstance().add_window(win);
    }
    else if (strcmp(argv[0], "browse") == 0) {
        BrowserApp* app = new BrowserApp();
        Window* win = new Window(150, 150, 800, 600, "ChucklesBrowse", app);
        WindowManager::getInstance().add_window(win);
        if (argc > 1) app->navigate(argv[1]);
    }
    else if (strcmp(argv[0], "term") == 0) {
        TerminalApp* app = new TerminalApp();
        Window* win = new Window(100, 100, 600, 400, "Terminal", app);
        WindowManager::getInstance().add_window(win);
    }
    else if (strcmp(argv[0], "edit") == 0) {
        const char* f = (argc > 1) ? argv[1] : "new.c";
        TextEditorApp* app = new TextEditorApp(f);
        Window* win = new Window(50, 50, 800, 600, f, app);
        WindowManager::getInstance().add_window(win);
    }
    else if (strcmp(argv[0], "disp") == 0) {
        DisplaySettingsApp* app = new DisplaySettingsApp();
        Window* win = new Window(100, 100, 280, 400, "Display Settings", app);
        WindowManager::getInstance().add_window(win);
    }

    // --- FILESYSTEM & DISK ---
    else if (strcmp(argv[0], "mkfs") == 0) {
        if(g_sata_port != -1) {
            Fat32::getInstance().format(g_sata_port, 131072);
        } else {
            printf("No SATA disk found.\n");
        }
    }
    else if (strcmp(argv[0], "mount") == 0) {
        if(g_sata_port != -1) Fat32::getInstance().init(g_sata_port);
    }
    else if (strcmp(argv[0], "ls") == 0) {
        Fat32::getInstance().ls();
    }

    // --- COMPILERS & LOADERS ---
    else if (strcmp(argv[0], "cpl") == 0) {
        if(argc > 2) cpl_compile(argv[1], argv[2]);
        else printf("Usage: cpl <source.cpl> <output.bin>\n");
    }
    else if (strcmp(argv[0], "ccc") == 0) {
        if(argc > 2) ccc_compile(argv[1], argv[2]);
        else printf("Usage: ccc <source.c> <output.bin>\n");
    }
    else if (strcmp(argv[0], "run") == 0) {
        if(argc > 1) HighLoader::load_and_run(argv[1]);
        else printf("Usage: run <binary.bin>\n");
    }
    else if (strcmp(argv[0], "exec") == 0) {
        if(argc > 1) RawLoader::load_and_run(argv[1], argc - 1, &argv[1]);
        else printf("Usage: exec <binary.bin>\n");
    }

    // --- SYSTEM UTILS ---
    else if (strcmp(argv[0], "help") == 0) {
        printf("GUI Apps: dvd, 3drnd, nes, browse, term, edit, disp\n");
        printf("System:   reboot, clear, sysinfo, lspci\n");
        printf("Dev:      cpl, ccc, run\n");
    }
    else if (strcmp(argv[0], "reboot") == 0) outb(0x64, 0xFE);
    else if (strcmp(argv[0], "clear") == 0) my_window->renderer->clear(0);
    else if (strcmp(argv[0], "sysinfo") == 0) {
        printf("ChucklesOS v3.0 Beta 2\n");
        printf("RAM: %d MB Used / %d MB Total\n", 
            (int)(pmm_get_used_memory()/1024/1024), 
            (int)(pmm_get_total_memory()/1024/1024));
    }
    else if (strcmp(argv[0], "lspci") == 0) lspci_run_detailed();
    else if (strcmp(argv[0], "netinit") == 0) E1000Driver::getInstance().init();
    else if (strcmp(argv[0], "usbinit") == 0) XhciDriver::getInstance().init(0x8086, 0x31A8);
    else {
        printf("Unknown command: %s\n", argv[0]);
    }
}