#ifndef DISPLAY_SETTINGS_APP_H
#define DISPLAY_SETTINGS_APP_H

#include "../gui/window.h"

class DisplaySettingsApp : public WindowApp {
public:
    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;
    void on_mouse(int rx, int ry, bool left_click) override;

private:
    Window* my_window;
    
    struct Button {
        int x, y, w, h;
        char label[32];
        int action_id;
    };
    
    static const int MAX_BTNS = 36;
    Button buttons[MAX_BTNS];
    int btn_count;
    
    bool editing_custom;
    
    struct Slider {
        int x, y, w, h;
        uint32_t* target_color;
        int shift;
    };
    
    Slider sliders[9];

    void init_buttons();
    void init_sliders();
    void draw_button(Button& b);
    void draw_slider(Slider& s);
    void apply_setting(int action_id);
};

#endif
