#include "system_widget.h"
#include "../render.h"           
#include "window.h"              
#include "../sys/system_stats.h" 
#include "../memory/pmm.h"
#include "../cppstd/stdio.h"
#include "../timer.h"
#include "../apps/display_settings.h" 

#define WIDGET_BG       0xCC000000 
#define WIDGET_BORDER   0x00FF00   
#define TEXT_TITLE      0x00FF00
#define TEXT_NORMAL     0xFFFFFF

void SystemWidget::render(Renderer* r, int screen_w) {
    SystemStats& stats = SystemStats::getInstance();
    uint64_t total_ram = pmm_get_total_memory();
    uint64_t used_ram = pmm_get_used_memory();
    
    int width = 220;
    int height = 430; 
    int x = screen_w - width - 10;
    int y = 10;
    int line_h = 16;
    
    // Background
    r->drawRect(x, y, width, height, 0x202020); 
    r->drawRect(x, y, width, 1, WIDGET_BORDER);
    r->drawRect(x, y + height - 1, width, 1, WIDGET_BORDER);
    r->drawRect(x, y, 1, height, WIDGET_BORDER);
    r->drawRect(x + width - 1, y, 1, height, WIDGET_BORDER);
    
    int cursor_y = y + 5;
    int cursor_x = x + 8;
    char buf[64];
    
    // Title
    r->drawString(cursor_x, cursor_y, "[ SYSTEM STATS ]", TEXT_TITLE);
    cursor_y += line_h + 5;
    
    // RAM
    sprintf(buf, "RAM: %d MB / %d MB", (int)(used_ram/1024/1024), (int)(total_ram/1024/1024));
    r->drawString(cursor_x, cursor_y, buf, TEXT_NORMAL);
    cursor_y += line_h;
    
    // Bar
    int bar_w = width - 16;
    int fill_w = (int)((double)used_ram / (double)total_ram * bar_w);
    r->drawRect(cursor_x, cursor_y, bar_w, 4, 0x404040);
    r->drawRect(cursor_x, cursor_y, fill_w, 4, 0x00FF00);
    cursor_y += 10;
    
    // Cores
    r->drawString(cursor_x, cursor_y, "--- CORES ---", 0xAAAAAA);
    cursor_y += line_h;
    
    for(int i=0; i<stats.cpu_count && i < 8; i++) {
        const char* spinner = "|/-\\";
        uint64_t ticks = stats.cpu_ticks[i];
        char spin_char = spinner[(ticks / 1000) % 4]; 
        sprintf(buf, "CPU %d: %c Online", i, spin_char);
        r->drawString(cursor_x, cursor_y, buf, TEXT_NORMAL);
        cursor_y += line_h;
    }
    
    // Services
    cursor_y += 5;
    r->drawString(cursor_x, cursor_y, "--- SERVICES ---", 0xAAAAAA);
    cursor_y += line_h;
    
    auto draw_srv = [&](const char* name, bool active) {
        sprintf(buf, "%s: %s", name, active ? "RUNNING" : "STOPPED");
        r->drawString(cursor_x, cursor_y, buf, active ? TEXT_NORMAL : 0x888888);
        cursor_y += line_h;
    };
    
    draw_srv("SMP Layer", stats.service_smp_active);
    draw_srv("PCI Bus", true); 
    draw_srv("PS/2 Input", stats.service_ps2_active);
    draw_srv("E1000 Net", stats.service_e1000_active);
    draw_srv("AHCI Disk", stats.service_ahci_active);
    
    // Button
    cursor_y += 10;
    int btn_x = cursor_x;
    int btn_w = width - 16;
    int btn_h = 20;
    
    r->drawRect(btn_x, cursor_y, btn_w, btn_h, 0x404040);
    r->drawString(btn_x + 30, cursor_y + 4, "DISPLAY SETTINGS", 0xFFFFFF);
}

void SystemWidget::render_process_list(Renderer* r, int screen_w, Window** windows, int count) {
    int width = 220;
    int right_margin = 10;
    int x = screen_w - width - right_margin;
    
    int cursor_y = 10 + 430 + 10; 
    int cursor_x = x + 8;
    
    r->drawString(cursor_x, cursor_y, "--- PROCESSES ---", 0xAAAAAA);
    cursor_y += 16;
    
    for(int i=0; i<count; i++) {
        if(windows[i]) {
            char title_buf[24];
            const char* src = windows[i]->title;
            int j=0;
            while(src[j] && j < 20) { title_buf[j] = src[j]; j++; }
            title_buf[j] = 0;
            char line[64];
            sprintf(line, "[%d] %s", i, title_buf);
            uint32_t col = windows[i]->is_focused ? 0x00FFFF : TEXT_NORMAL;
            r->drawString(cursor_x, cursor_y, line, col);
            cursor_y += 16;
        }
    }
}