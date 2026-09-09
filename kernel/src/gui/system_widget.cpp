#include "system_widget.h"
#include "../render.h"
#include "window.h"
#include "../sys/system_stats.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../cppstd/stdio.h"
#include "../timer.h"

void SystemWidget::render(Renderer* r, int screen_w) {
    SystemStats& stats = SystemStats::getInstance();

    // Query both Dynamic Virtual Heap and Physical Hardware RAM
    size_t heap_used = heap_get_used();
    size_t heap_total = heap_get_total();
    uint64_t phys_used = pmm_get_used_memory();
    uint64_t phys_total = pmm_get_total_memory();

    int width = 230;
    int height = 310;
    int x = screen_w - width - 16;
    int y = 16;
    int line_h = 18;

    // Card background & outline
    r->drawRect(x, y, width, height, 0x1E1E26);
    r->drawRect(x, y, width, 1, 0x2E2E3C);
    r->drawRect(x, y + height - 1, width, 1, 0x2E2E3C);
    r->drawRect(x, y, 1, height, 0x2E2E3C);
    r->drawRect(x + width - 1, y, 1, height, 0x2E2E3C);

    int cur_y = y + 12;
    int cur_x = x + 14;
    char buf[64];

    r->drawString(cur_x, cur_y, "System Resources", 0xEAEAEA);
    cur_y += line_h + 6;

    // 1. DYNAMIC HEAP USAGE (Accurately reflects malloc() and free())
    int heap_used_mb = (int)(heap_used / 1024 / 1024);
    int heap_used_kb = (int)((heap_used % (1024 * 1024)) / 1024);
    int heap_tot_mb  = (int)(heap_total / 1024 / 1024);
    
    if (heap_tot_mb == 0) heap_tot_mb = 1;

    sprintf(buf, "Heap: %d.%01d / %d MB", heap_used_mb, heap_used_kb / 100, heap_tot_mb);
    r->drawString(cur_x, cur_y, buf, 0xC5C6D0);
    cur_y += line_h - 2;

    int bar_w = width - 28;
    int fill_w = (int)(((double)heap_used / (double)heap_total) * bar_w);
    if (fill_w > bar_w) fill_w = bar_w;
    if (fill_w < 2 && heap_used > 0) fill_w = 2;

    r->drawRect(cur_x, cur_y, bar_w, 4, 0x2B2B38);
    r->drawRect(cur_x, cur_y, fill_w, 4, 0x6C72CB);
    cur_y += 12;

    // 2. PHYSICAL RAM
    sprintf(buf, "Phys RAM: %d / %d MB", (int)(phys_used / 1024 / 1024), (int)(phys_total / 1024 / 1024));
    r->drawString(cur_x, cur_y, buf, 0x7E8096);
    cur_y += line_h + 4;

    // 3. PROCESSORS
    r->drawString(cur_x, cur_y, "Processors", 0x5C5E70);
    cur_y += line_h;

    for (int i = 0; i < stats.cpu_count && i < 4; i++) {
        uint64_t ticks = stats.cpu_ticks[i];
        const char* spinner = "|/-\\";
        char s = spinner[(ticks / 1000) % 4];
        sprintf(buf, "Core %d:  Active [%c]", i, s);
        r->drawString(cur_x, cur_y, buf, 0xC5C6D0);
        cur_y += line_h;
    }

    cur_y += 4;
    r->drawString(cur_x, cur_y, "Subsystems", 0x5C5E70);
    cur_y += line_h;

    auto draw_tag = [&](const char* name, bool ok) {
        r->drawRect(cur_x, cur_y + 4, 6, 6, ok ? 0x2ED573 : 0xFF4757);
        r->drawString(cur_x + 12, cur_y, name, ok ? 0xC5C6D0 : 0x707280);
        cur_y += line_h;
    };

    draw_tag("AHCI Storage Engine", stats.service_ahci_active);
    draw_tag("SMP Multi-Threading", stats.service_smp_active);
    draw_tag("PS/2 Device Pipeline", stats.service_ps2_active);
}

void SystemWidget::render_process_list(Renderer* r, int screen_w, Window** windows, int count) {
    int width = 230;
    int x = screen_w - width - 16;
    int cur_y = 16 + 310 + 10;
    int cur_x = x + 14;

    r->drawRect(x, cur_y - 6, width, 24 + (count * 20), 0x1E1E26);
    r->drawRect(x, cur_y - 6, width, 1, 0x2E2E3C);
    r->drawRect(x, cur_y + 18 + (count * 20) - 1, width, 1, 0x2E2E3C);
    r->drawRect(x, cur_y - 6, 1, 24 + (count * 20), 0x2E2E3C);
    r->drawRect(x + width - 1, cur_y - 6, 1, 24 + (count * 20), 0x2E2E3C);

    r->drawString(cur_x, cur_y, "Active Desktops", 0x5C5E70);
    cur_y += 18;

    for (int i = 0; i < count; i++) {
        if (!windows[i]) continue;
        char name[24];
        int j = 0;
        while (windows[i]->title[j] && j < 18) {
            name[j] = windows[i]->title[j];
            j++;
        }
        name[j] = '\0';

        char row[48];
        sprintf(row, "%s", name);

        if (windows[i]->is_focused) {
            r->drawRect(x + 6, cur_y - 2, width - 12, 16, 0x262738);
            r->drawString(cur_x + 4, cur_y, row, 0xFFFFFF);
        } else {
            r->drawString(cur_x + 4, cur_y, row, 0x7E8096);
        }
        cur_y += 20;
    }
}
