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
#include "../apps/display_settings.h"
#include "../apps/lang_vm.h" // Added for the Scripting VM

// Hardware/System Headers
#include "../pci/lspci.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"
#include "../fs/fat32.h"
#include "../io.h"

extern int g_sata_port;

void TerminalApp::on_init(Window* win) {
    my_window = win;
    input_index = 0;
    memset(input_buffer, 0, sizeof(input_buffer));
    
    g_console = win->console;
    
    win->renderer->clear(0x000000);
    win->console->setColor(0xFFFFFF, 0x000000);
    win->console->print("ChucklesOS Shell\n$ ");
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
    // --- SCRIPTING VM ---
    else if (strcmp(argv[0], "run") == 0) {
        if (argc > 1) {
            char* file_buf = (char*)malloc(65536);
            if (Fat32::getInstance().read_file(argv[1], file_buf, 65536)) {
                LangVM vm;
                vm.run_script(file_buf);
            } else {
                printf("Error: Could not read script file '%s'\n", argv[1]);
            }
            free(file_buf);
        } else {
            printf("Usage: run <filename.cs>\n");
        }
    }
    // --- FILESYSTEM & SYSTEM ---
    else if (strcmp(argv[0], "ls") == 0) {
        Fat32::getInstance().ls();
    }
    else if (strcmp(argv[0], "format") == 0) {
        if (g_sata_port != -1) {
            // Defaulting to 128MB (262,144 sectors of 512 bytes)
            uint32_t sectors = 262144; 
            printf("Formatting disk on SATA port %d (128MB)...\n", g_sata_port);
            if (Fat32::getInstance().format(g_sata_port, sectors)) {
                printf("Format successful! Wiped and initialized FAT32.\n");
            } else {
                printf("Format failed.\n");
            }
        } else {
            printf("Error: No SATA disk found to format.\n");
        }
    }
    else if (strcmp(argv[0], "help") == 0) {
        printf("GUI Apps: dvd, 3drnd, nes, browse, term, edit, disp\n");
        printf("System:   ls, format, run, reboot, clear, sysinfo, lspci\n");
    }
    else if (strcmp(argv[0], "reboot") == 0) {
        outb(0x64, 0xFE);
    }
    else if (strcmp(argv[0], "clear") == 0) {
        my_window->renderer->clear(0);
    }
    else if (strcmp(argv[0], "sysinfo") == 0) {
        printf("ChucklesOS v3.0\n");
        printf("RAM: %d MB Used / %d MB Total\n", 
            (int)(pmm_get_used_memory()/1024/1024), 
            (int)(pmm_get_total_memory()/1024/1024));
    }
    else if (strcmp(argv[0], "lspci") == 0) {
        lspci_run_detailed();
    }
    else {
        printf("Unknown command: %s\n", argv[0]);
    }
}