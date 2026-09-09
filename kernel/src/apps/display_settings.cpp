#include "display_settings.h"
#include "../globals.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../input.h"

void DisplaySettingsApp::on_init(Window* win) {
    my_window = win;
    editing_custom = false;
    init_buttons();
    init_sliders();
}

void DisplaySettingsApp::init_buttons() {
    btn_count = 0;
    int y = 28;

    // 1. Color Filters (Left Column: x = 14, w = 120)
    buttons[btn_count++] = {14, y, 120, 20, "32-Bit TrueColor", MODE_32BIT}; y += 24;
    buttons[btn_count++] = {14, y, 120, 20, "24-Bit Clean", MODE_24BIT}; y += 24;
    buttons[btn_count++] = {14, y, 120, 20, "16-Bit Muted", MODE_16BIT}; y += 24;
    buttons[btn_count++] = {14, y, 120, 20, "8-Bit Quantized", MODE_8BIT}; y += 24;
    buttons[btn_count++] = {14, y, 120, 20, "Grayscale Studio", MODE_GRAYSCALE}; y += 24;
    buttons[btn_count++] = {14, y, 120, 20, "CGA Retro", MODE_4COLOR}; y += 24;
    buttons[btn_count++] = {14, y, 120, 20, "High Contrast", MODE_MONOCHROME}; y += 24;

    // 2. Color Palettes
    int theme_y = y + 16;
    buttons[btn_count++] = {14, theme_y, 120, 20, "Dark Slate", 20}; theme_y += 24;
    buttons[btn_count++] = {14, theme_y, 120, 20, "Nordic Frost", 21}; theme_y += 24;
    buttons[btn_count++] = {14, theme_y, 120, 20, "Obsidian Black", 22}; theme_y += 24;
    buttons[btn_count++] = {14, theme_y, 120, 20, "Solar Light", 23}; theme_y += 24;
    buttons[btn_count++] = {14, theme_y, 120, 20, "Custom Tuner", 30};

    // 3. Hardware Display Outputs (Right Column: x = 148, w = 128)
    y = 28;
    char native_str[32];
    sprintf(native_str, "Native (%dx%d)", (int)g_hardware_display.native_width, (int)g_hardware_display.native_height);
    Button b_native = {148, y, 128, 20, "", 10};
    strcpy(b_native.label, native_str);
    buttons[btn_count++] = b_native;
    y += 24;

    for (uint32_t i = 0; i < g_hardware_display.mode_count && btn_count < MAX_BTNS; i++) {
        uint32_t mw = g_hardware_display.modes[i].width;
        uint32_t mh = g_hardware_display.modes[i].height;

        char res_str[32];
        if (mw == 1600 && mh == 900) {
            sprintf(res_str, "1600x900 [Def]");
        } else {
            sprintf(res_str, "%d x %d", (int)mw, (int)mh);
        }

        Button b = {148, y, 128, 20, "", (int)(100 + i)};
        strcpy(b.label, res_str);
        buttons[btn_count++] = b;
        y += 24;
        if (y > 360) break;
    }
}

void DisplaySettingsApp::init_sliders() {
    int x = 20; int y = 40;
    uint32_t* targets[] = { &g_theme.title_active, &g_theme.win_bg, &g_theme.desktop_bg };

    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) {
            int shift = (2 - c) * 8;
            sliders[i * 3 + c] = { x + (c * 85), y, 80, 16, targets[i], shift };
        }
        y += 50;
    }
}

void DisplaySettingsApp::draw_button(Button& b) {
    Renderer* r = my_window->renderer;

    // Modern flat button styling
    r->drawRect(b.x, b.y, b.w, b.h, 0x262734);
    r->drawRect(b.x, b.y, b.w, 1, 0x36384C);
    r->drawRect(b.x, b.y + b.h - 1, b.w, 1, 0x1A1A24);
    r->drawRect(b.x, b.y, 1, b.h, 0x36384C);
    r->drawRect(b.x + b.w - 1, b.y, 1, b.h, 0x1A1A24);

    r->drawString(b.x + 8, b.y + 5, b.label, 0xD4D6E2);
}

void DisplaySettingsApp::draw_slider(Slider& s) {
    Renderer* r = my_window->renderer;
    r->drawRect(s.x, s.y + 6, s.w, 4, 0x2B2D3D);

    uint32_t val = (*s.target_color >> s.shift) & 0xFF;
    int kx = s.x + (int)((val / 255.0f) * (s.w - 8));

    // Pill thumb
    r->drawRect(kx, s.y, 8, 16, 0x6C72CB);
    r->drawRect(kx, s.y, 8, 1, 0x8D93E8);

    uint32_t col = 0;
    if (s.shift == 16) col = 0xFF5F56;
    if (s.shift == 8)  col = 0x2ED573;
    if (s.shift == 0)  col = 0x1E90FF;
    r->drawRect(s.x + s.w / 2 - 2, s.y - 6, 4, 4, col);
}

void DisplaySettingsApp::on_draw() {
    my_window->renderer->clear(g_theme.win_bg);

    if (!editing_custom) {
        my_window->renderer->drawString(14, 10, "COLOR FILTER", 0x5C5E70);
        my_window->renderer->drawString(14, 202, "PALETTE", 0x5C5E70);
        my_window->renderer->drawString(148, 10, "RESOLUTIONS", 0x5C5E70);

        for (int i = 0; i < btn_count; i++) {
            draw_button(buttons[i]);
        }
    } else {
        my_window->renderer->drawString(14, 10, "Palette Tuner", 0xFFFFFF);
        my_window->renderer->drawString(14, 25, "Header Color (RGB)", 0x8C8EA0);
        my_window->renderer->drawString(14, 75, "Window Background (RGB)", 0x8C8EA0);
        my_window->renderer->drawString(14, 125, "Desktop Backdrop (RGB)", 0x8C8EA0);

        for (int i = 0; i < 9; i++) draw_slider(sliders[i]);

        Button back = { 14, 200, 80, 24, "Done", 40 };
        draw_button(back);
    }
}

void DisplaySettingsApp::on_input(char c) { (void)c; }

void DisplaySettingsApp::on_mouse(int rx, int ry, bool left_click) {
    if (!left_click) return;

    if (!editing_custom) {
        for (int i = 0; i < btn_count; i++) {
            Button& b = buttons[i];
            if (rx >= b.x && rx <= b.x + b.w && ry >= b.y && ry <= b.y + b.h) {
                apply_setting(b.action_id);
                return;
            }
        }
    } else {
        for (int i = 0; i < 9; i++) {
            Slider& s = sliders[i];
            if (rx >= s.x - 5 && rx <= s.x + s.w + 5 && ry >= s.y && ry <= s.y + s.h) {
                int val = (int)(((float)(rx - s.x) / (float)(s.w)) * 255.0f);
                if (val < 0) val = 0;
                if (val > 255) val = 255;

                uint32_t mask = ~(0xFF << s.shift);
                *s.target_color = (*s.target_color & mask) | (val << s.shift);
                return;
            }
        }
        if (rx >= 14 && rx <= 94 && ry >= 200 && ry <= 224) {
            editing_custom = false;
        }
    }
}

void DisplaySettingsApp::apply_setting(int id) {
    if (id < 10) {
        g_display_settings.mode = (DisplayMode)id;
    } 
    else if (id == 10) {
        g_display_settings.target_width = 0;
        g_display_settings.target_height = 0;
        g_display_settings.dirty = true;
    }
    else if (id >= 100 && id < 200) {
        int idx = id - 100;
        if (idx >= 0 && idx < (int)g_hardware_display.mode_count) {
            g_display_settings.target_width = g_hardware_display.modes[idx].width;
            g_display_settings.target_height = g_hardware_display.modes[idx].height;
            g_display_settings.dirty = true;
        }
    }
    else if (id >= 20 && id < 30) {
        // Dark Slate (Default)
        if (id == 20) g_theme = { 0x1E1E24, 0x2E2E38, 0x262630, 0x1B1B22, 0x5E60CE, 0xEDEDF0, 0x868798, 0xFF5F56, 0x121216, 0x18181E };
        // Nordic Frost
        if (id == 21) g_theme = { 0x2E3440, 0x434C5E, 0x3B4252, 0x282D37, 0x88C0D0, 0xECEFF4, 0xD8DEE9, 0xBF616A, 0x242831, 0x282D37 };
        // Obsidian Black
        if (id == 22) g_theme = { 0x141416, 0x222226, 0x1A1A1E, 0x101012, 0x7B2CBF, 0xF5F5F7, 0x6E6E78, 0xE63946, 0x0A0A0C, 0x121214 };
        // Solar Light
        if (id == 23) g_theme = { 0xF4F5F8, 0xD4D6DF, 0xE9EBEF, 0xDDE0E6, 0x4361EE, 0x1A1A22, 0x727586, 0xE74C3C, 0xE2E4EB, 0xD7DAE3 };
    }
    else if (id == 30) {
        editing_custom = true;
    }
}
