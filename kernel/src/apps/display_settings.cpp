#include "display_settings.h"
#include "../globals.h"
#include "../cppstd/stdio.h"
#include "../input.h" 

void DisplaySettingsApp::on_init(Window* win) {
    my_window = win;
    editing_custom = false;
    init_buttons();
    init_sliders();
}

void DisplaySettingsApp::init_buttons() {
    int y = 30;
    // Mode Buttons
    buttons[0] = {10, y, 120, 20, "32-Bit (Def)", MODE_32BIT}; y+=25;
    buttons[1] = {10, y, 120, 20, "24-Bit", MODE_24BIT}; y+=25;
    buttons[2] = {10, y, 120, 20, "16-Bit Retro", MODE_16BIT}; y+=25;
    buttons[3] = {10, y, 120, 20, "8-Bit ", MODE_8BIT}; y+=25;
    buttons[4] = {10, y, 120, 20, "Grayscale", MODE_GRAYSCALE}; y+=25;
    buttons[5] = {10, y, 120, 20, "CGA 4-Color", MODE_4COLOR}; y+=25;
    buttons[6] = {10, y, 120, 20, "1-Bit Mono", MODE_MONOCHROME}; y+=25;
    
    // Resolution Buttons
    y = 30;
    buttons[7] = {150, y, 120, 20, "Native Res", 10}; y+=25;
    buttons[8] = {150, y, 120, 20, "640 x 480", 11}; y+=25;
    buttons[9] = {150, y, 120, 20, "320 x 240", 12}; y+=25;
    
    // Theme Buttons
    y += 10;
    buttons[10] = {150, y, 120, 20, "Theme: Dark", 20}; y+=25;
    buttons[11] = {150, y, 120, 20, "Theme: Mono", 21}; y+=25;
    buttons[12] = {150, y, 120, 20, "Theme: Win95", 22}; y+=25;
    buttons[13] = {150, y, 120, 20, "Theme: Simple", 23}; y+=25;
    buttons[14] = {150, y, 120, 20, "Theme: Custom", 24}; y+=25;
    buttons[15] = {150, y, 120, 20, "Edit Custom...", 30};
}

void DisplaySettingsApp::init_sliders() {
    // 3 Groups: Title Start, Title End, Window BG
    int x = 20; int y = 40;
    uint32_t* targets[] = { &g_theme.title_active_1, &g_theme.title_active_2, &g_theme.win_bg };
    
    for(int i=0; i<3; i++) {
        for(int c=0; c<3; c++) {
            int shift = (2-c) * 8;
            sliders[i*3 + c] = { x + (c * 85), y, 80, 20, targets[i], shift };
        }
        y += 50;
    }
}

void DisplaySettingsApp::draw_button(Button& b) {
    Renderer* r = my_window->renderer;
    uint32_t bg = g_theme.btn_face;
    uint32_t light = g_theme.border_light;
    uint32_t dark = g_theme.border_dark;
    uint32_t text = g_theme.text_color;

    r->drawRect(b.x, b.y, b.w, b.h, bg);
    
    if (!g_theme.flat) {
        r->drawRect(b.x, b.y, b.w, 1, light); 
        r->drawRect(b.x, b.y, 1, b.h, light); 
        r->drawRect(b.x, b.y + b.h - 1, b.w, 1, dark); 
        r->drawRect(b.x + b.w - 1, b.y, 1, b.h, dark); 
    } else {
        r->drawRect(b.x, b.y, b.w, 1, dark);
        r->drawRect(b.x, b.y + b.h - 1, b.w, 1, dark);
        r->drawRect(b.x, b.y, 1, b.h, dark);
        r->drawRect(b.x + b.w - 1, b.y, 1, b.h, dark);
    }
    
    r->drawString(b.x + 5, b.y + 4, b.label, text);
}

void DisplaySettingsApp::draw_slider(Slider& s) {
    Renderer* r = my_window->renderer;
    // Background bar
    r->drawRect(s.x, s.y + 8, s.w, 4, 0x808080);
    
    // Knob position (0..255 mapped to 0..s.w)
    uint32_t val = (*s.target_color >> s.shift) & 0xFF;
    int kx = s.x + (int)((val / 255.0f) * (s.w - 8));
    
    // Draw Knob
    r->drawRect(kx, s.y, 8, 20, g_theme.btn_face);
    r->drawRect(kx, s.y, 8, 1, g_theme.border_light);
    r->drawRect(kx, s.y, 1, 20, g_theme.border_light);
    r->drawRect(kx+7, s.y, 1, 20, g_theme.border_dark);
    r->drawRect(kx, s.y+19, 8, 1, g_theme.border_dark);
    
    // Label color component
    uint32_t col = 0;
    if (s.shift == 16) col = 0xFF0000;
    if (s.shift == 8) col = 0x00FF00;
    if (s.shift == 0) col = 0x0000FF;
    r->drawRect(s.x + s.w/2 - 2, s.y - 5, 4, 4, col);
}

void DisplaySettingsApp::on_draw() {
    my_window->renderer->clear(g_theme.win_bg);
    uint32_t txt = g_theme.text_color;

    if (!editing_custom) {
        my_window->renderer->drawString(10, 5, "Display Mode:", txt);
        my_window->renderer->drawString(150, 5, "Resolution:", txt);
        my_window->renderer->drawString(150, 115, "Theme:", txt);
        
        for(int i=0; i<BTN_COUNT; i++) {
            draw_button(buttons[i]);
        }
    } else {
        // Custom Editor
        my_window->renderer->drawString(10, 5, "Edit Custom Theme:", txt);
        
        my_window->renderer->drawString(10, 25, "Title Gradient Start (RGB)", txt);
        my_window->renderer->drawString(10, 75, "Title Gradient End (RGB)", txt);
        my_window->renderer->drawString(10, 125, "Window Background (RGB)", txt);
        
        for(int i=0; i<9; i++) draw_slider(sliders[i]);
        
        // Back Button
        Button back = { 10, 200, 80, 25, "Back", 40 };
        draw_button(back);
    }
}

void DisplaySettingsApp::on_input(char c) { (void)c; }

void DisplaySettingsApp::on_mouse(int rx, int ry, bool left_click) {
    if (!left_click) return;
    
    if (!editing_custom) {
        for(int i=0; i<BTN_COUNT; i++) {
            Button& b = buttons[i];
            if (rx >= b.x && rx <= b.x + b.w && ry >= b.y && ry <= b.y + b.h) {
                apply_setting(b.action_id);
                return;
            }
        }
    } else {
        // Handle Sliders
        for(int i=0; i<9; i++) {
            Slider& s = sliders[i];
            // Hitbox generous for slider
            if (rx >= s.x - 5 && rx <= s.x + s.w + 5 && ry >= s.y && ry <= s.y + s.h) {
                // Calculate new value
                int val = (int)(((float)(rx - s.x) / (float)(s.w)) * 255.0f);
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                
                // Update mask
                uint32_t mask = ~(0xFF << s.shift);
                *s.target_color = (*s.target_color & mask) | (val << s.shift);
                return;
            }
        }
        // Handle Back
        if (rx >= 10 && rx <= 90 && ry >= 200 && ry <= 225) {
            editing_custom = false;
        }
    }
}

void DisplaySettingsApp::apply_setting(int id) {
    if (id < 10) {
        g_display_settings.mode = (DisplayMode)id;
    } 
    else if (id < 20) {
        if (id == 10) { g_display_settings.target_width = 0; g_display_settings.target_height = 0; }
        if (id == 11) { g_display_settings.target_width = 640; g_display_settings.target_height = 480; }
        if (id == 12) { g_display_settings.target_width = 320; g_display_settings.target_height = 240; }
        g_display_settings.dirty = true;
    }
    else if (id < 30) {
        // Theme Apply
        // Dark (Default)
        if (id == 20) g_theme = { 0x303030, 0x505050, 0x101010, 0x000040, 0x000080, 0x404040, 0xFFFFFF, 0x404040, false };
        // Monochrome
        if (id == 21) g_theme = { 0xFFFFFF, 0x000000, 0x000000, 0x000000, 0x000000, 0xFFFFFF, 0x000000, 0xFFFFFF, true };
        // Win95
        if (id == 22) g_theme = { 0xC0C0C0, 0xFFFFFF, 0x000000, 0x000080, 0x1084D0, 0x808080, 0xFFFFFF, 0xC0C0C0, false };
        // Simple
        if (id == 23) g_theme = { 0xDDDDDD, 0xAAAAAA, 0x444444, 0x888888, 0x888888, 0xAAAAAA, 0x000000, 0xEEEEEE, true };
        // Custom (Just flag it, values persist)
        if (id == 24) { /* Values already set by sliders */ }
    }
    else if (id == 30) {
        editing_custom = true;
    }
}