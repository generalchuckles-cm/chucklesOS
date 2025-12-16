#ifndef DISPLAY_SETTINGS_APP_H
#define DISPLAY_SETTINGS_APP_H

#include "../gui/window.h"

class DisplaySettingsApp : public WindowApp {
public:
    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;
    
    // Handle mouse clicks passed from Window class
    void on_mouse(int rx, int ry, bool left_click) override;

private:
    Window* my_window;
    
    struct Button {
        int x, y, w, h;
        const char* label;
        int action_id; // 0-6: Modes, 10-12: Res, 20-24: Themes, 30: Edit Custom, 40: Back
    };
    
    static const int BTN_COUNT = 16; // Increased for Themes
    Button buttons[BTN_COUNT];
    
    bool editing_custom;
    
    // Sliders for custom theme [R, G, B] for [Title1, Title2, BG]
    // 3 components * 3 targets = 9 sliders
    struct Slider {
        int x, y, w, h;
        uint32_t* target_color; // Pointer to g_theme color to edit
        int shift; // 16 for R, 8 for G, 0 for B
    };
    
    Slider sliders[9];

    void init_buttons();
    void init_sliders();
    void draw_button(Button& b);
    void draw_slider(Slider& s);
    void apply_setting(int action_id);
    void update_slider(int mx, int my);
};

#endif