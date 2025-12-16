#ifndef DVD_H
#define DVD_H

#include "gui/window.h"

class DVDApp : public WindowApp {
public:
    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;

private:
    Window* my_window;
    float x, y;
    float vx, vy;
    int current_color_idx;
};

#endif