#include "dvd.h"
#include "dvd_image.h"
#include "cppstd/stdlib.h"
#include "cppstd/math.h"
#include "timer.h"

static uint32_t dvd_colors[] = {
    0xFFFFFF, 0xFF0000, 0x00FF00, 0x00FFFF, 
    0x0000FF, 0xA52A2A, 0xFFFF00, 0xFFA500
};

void DVDApp::on_init(Window* win) {
    my_window = win;
    x = win->width / 2;
    y = win->height / 2;
    vx = 3.0f; 
    vy = 3.0f;
    current_color_idx = 0;
    
    // Initial clear
    win->renderer->clear(0x000000);
}

void DVDApp::on_input(char c) {
    (void)c; // Ignore input
}

void DVDApp::on_draw() {
    int img_w = dvd_image_WIDTH;
    int img_h = dvd_image_HEIGHT;
    
    // 1. Clear previous position (Dirty rect)
    // We expand slightly to ensure no trails
    my_window->renderer->drawRect((int)x - 4, (int)y - 4, img_w + 8, img_h + 8, 0x000000);

    // 2. Physics
    x += vx;
    y += vy;

    bool bounced = false;
    if (x <= 0) { x = 0; vx = -vx; bounced = true; }
    else if (x + img_w >= my_window->width) { x = my_window->width - img_w; vx = -vx; bounced = true; }

    if (y <= 0) { y = 0; vy = -vy; bounced = true; }
    else if (y + img_h >= my_window->height) { y = my_window->height - img_h; vy = -vy; bounced = true; }

    if (bounced) {
        current_color_idx = (current_color_idx + 1) % 8;
    }

    // 3. Render
    my_window->renderer->renderBitmapColored((int)x, (int)y, img_w, img_h, g_dvd_image_data, dvd_colors[current_color_idx]);
}