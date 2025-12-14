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
#include "../loader/raw_loader.h"
#include "../drv/usb/xhci.h"
#include "../drv/storage/ahci.h"
#include "../drv/net/e1000.h"
#include "../input.h" 
#include "../net/network.h" 
#include "../net/tcp.h"
#include "../sys/chuckles_daemon.h"

// --- App Headers ---
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
    win->console->print("ChucklesOS Shell (CPL Ready)\n$ ");
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

    // --- CPL COMPILER ---
    if (strcmp(argv[0], "cpl") == 0) {
        if(argc > 2) cpl_compile(argv[1], argv[2]);
        else printf("Usage: cpl <source.cpl> <output.bin>\n");
    }
    // --- BROWSER COMMANDS ---
    else if (strcmp(argv[0], "browse") == 0) {
        if (argc > 1) {
            BrowserApp* app = new BrowserApp();
            Window* win = new Window(150, 150, 800, 600, "ChucklesBrowse", app);
            WindowManager::getInstance().add_window(win);
            app->navigate(argv[1]);
        } else {
            printf("Usage: browse <url>\n");
        }
    }
    else if (strcmp(argv[0], "httpget") == 0) {
        if (argc > 1) {
            char* host = argv[1];
            uint32_t ip = NetworkStack::getInstance().dns_lookup(host);
            if (ip == 0) {
                printf("Error: Could not resolve %s\n", host);
            } else {
                printf("Connecting to %d.%d.%d.%d...\n", 
                    ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
                
                TcpSocket sock;
                if (sock.connect(ip, 80)) {
                    char request[256];
                    sprintf(request, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
                    sock.send((uint8_t*)request, strlen(request));
                    
                    uint8_t buf[1024];
                    int total = 0;
                    while(true) {
                        int r = sock.recv(buf, 1023);
                        if (r <= 0) break;
                        buf[r] = 0;
                        printf("%s", (char*)buf); 
                        total += r;
                    }
                    printf("\n\n[Closed. Total: %d bytes]\n", total);
                    sock.close();
                } else {
                    printf("Connect Failed.\n");
                }
            }
        } else {
            printf("Usage: httpget <host>\n");
        }
    }
    // --- NETWORK COMMANDS ---
    else if (strcmp(argv[0], "chameon") == 0) {
        if (argc > 1 && strcmp(argv[1], "reload") == 0) {
            ChucklesDaemon::getInstance().reload_network_config();
        } else {
            printf("Usage: chameon reload\n");
        }
    }
    else if (strcmp(argv[0], "nslookup") == 0) {
        if (argc > 1) {
            uint32_t ip = NetworkStack::getInstance().dns_lookup(argv[1]);
            if (ip) {
                printf("%s -> %d.%d.%d.%d\n", argv[1], 
                    ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
            } else {
                printf("DNS Resolution failed.\n");
            }
        } else {
            printf("Usage: nslookup <hostname>\n");
        }
    }
    else if (strcmp(argv[0], "ping") == 0) {
        if (argc > 1) {
            int ms = NetworkStack::getInstance().ping(argv[1]);
            if (ms >= 0) printf("Reply: %dms\n", ms);
            else printf("Timeout.\n");
        }
    }
    else if (strcmp(argv[0], "netinit") == 0) E1000Driver::getInstance().init();
    
    // --- SYSTEM COMMANDS ---
    else if (strcmp(argv[0], "help") == 0) {
        printf("Commands: help, reboot, clear, sysinfo, lspci, free\n");
        printf("Demos:    stress, dvd, 3drnd, playmidi, nes <rom>\n");
        printf("Files:    ls, edit, mkfs, mount\n");
        printf("Dev:      cpl <src> <out>, exec <bin>, cc <src> <out>\n");
        printf("Net:      ping, nslookup, httpget, browse, netinit, chameon\n");
        printf("Hardware: usbinit\n");
    }
    else if (strcmp(argv[0], "reboot") == 0) {
        outb(0x64, 0xFE);
    }
    else if (strcmp(argv[0], "clear") == 0) my_window->renderer->clear(0);
    else if (strcmp(argv[0], "sysinfo") == 0) {
        printf("ChucklesOS v3.0\n");
        printf("RAM: %d MB Used / %d MB Total\n", 
            (int)(pmm_get_used_memory()/1024/1024), 
            (int)(pmm_get_total_memory()/1024/1024));
        printf("CPU Speed: %d MHz\n", (int)(get_cpu_frequency()/1000000));
    }
    else if (strcmp(argv[0], "lspci") == 0) lspci_run_detailed();
    else if (strcmp(argv[0], "free") == 0) heap_print_stats();
    
    // --- DEMOS & DRIVERS ---
    else if (strcmp(argv[0], "stress") == 0) run_stress_test(my_window->renderer);
    else if (strcmp(argv[0], "dvd") == 0) run_dvd_demo(my_window->renderer);
    else if (strcmp(argv[0], "3drnd") == 0) run_3d_demo(my_window->renderer);
    else if (strcmp(argv[0], "playmidi") == 0) play_midi(nullptr, 0, nullptr, nullptr);
    else if (strcmp(argv[0], "usbinit") == 0) XhciDriver::getInstance().init(0x8086, 0x31A8);
    
    // --- FILESYSTEM ---
    else if (strcmp(argv[0], "mount") == 0) {
        if (g_sata_port != -1) Fat32::getInstance().init(g_sata_port);
    }
    else if (strcmp(argv[0], "ls") == 0) Fat32::getInstance().ls();
    else if (strcmp(argv[0], "mkfs") == 0) {
        if(g_sata_port != -1) Fat32::getInstance().format(g_sata_port, 65536);
    }
    
    // --- TOOLS ---
    else if (strcmp(argv[0], "edit") == 0) {
        if(argc>1) run_text_editor(argv[1]);
    }
    else if (strcmp(argv[0], "exec") == 0) {
        if(argc>1) RawLoader::load_and_run(argv[1], argc - 1, &argv[1]);
    }
    else if (strcmp(argv[0], "cc") == 0) {
        if(argc>2) run_compiler(argv[1], argv[2]);
    }
    else if (strcmp(argv[0], "nes") == 0) {
        if(argc>1) run_nes(argv[1]);
        else run_nes(NULL);
    }
    else {
        printf("Unknown: %s\n", argv[0]);
    }
}