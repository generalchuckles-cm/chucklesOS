#include "dvd.h"
#include "dvd_image.h"
#include "timer.h"
#include "input.h"
#include "cppstd/stdlib.h"
#include "cppstd/stdio.h"
#include "cppstd/math.h" 

static uint32_t dvd_colors[] = {
    0xFFFFFF, 0xFF0000, 0x00FF00, 0x00FFFF, 
    0x0000FF, 0xA52A2A, 0xFFFF00, 0xFFA500
};

static int pi_digits[] = {
    1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 
    8, 9, 7, 9, 3, 2, 3, 8, 4, 6, 
    2, 6, 4, 3, 3, 8, 3, 2, 7, 9
};

static void rotate_vector(float* vx, float* vy, float angle_rad) {
    float c = cos(angle_rad);
    float s = sin(angle_rad);
    float new_vx = (*vx * c) - (*vy * s);
    float new_vy = (*vx * s) + (*vy * c);
    *vx = new_vx;
    *vy = new_vy;
}

void run_dvd_demo(Renderer* r, uint64_t override_speed, uint64_t fps_mode) {
    bool running = true;
    srand((unsigned int)rdtsc_serialized());

    uint64_t cpu_freq;
    if (override_speed > 0) cpu_freq = override_speed * 100000000;
    else cpu_freq = get_cpu_frequency();

    uint64_t ticks_per_step = cpu_freq / 60;

    float x = (float)(r->getWidth() / 2);
    float y = (float)(r->getHeight() / 2);
    
    float speed = 6.0f;
    float vx = 4.24f;
    float vy = 4.24f;

    int last_render_x = (int)x;
    int last_render_y = (int)y;
    bool first_frame = true;

    int current_color_idx = 0;
    uint32_t active_color = dvd_colors[0]; 

    int screen_w = (int)r->getWidth();
    int screen_h = (int)r->getHeight();
    int img_w = dvd_image_WIDTH;
    int img_h = dvd_image_HEIGHT;

    int pi_index = 0;

    r->clear(0x000000);

    uint64_t frame_counter = 0;

    while (running) {
        // --- INPUT ---
        char c = input_check_char();
        if (c == 27) running = false;
        // -------------
        
        // Physics
        x += vx;
        y += vy;

        bool bounced = false;

        if (x <= 0) { x = 0; vx = -vx; bounced = true; } 
        else if (x + img_w >= screen_w) { x = (float)(screen_w - img_w); vx = -vx; bounced = true; }

        if (y <= 0) { y = 0; vy = -vy; bounced = true; } 
        else if (y + img_h >= screen_h) { y = (float)(screen_h - img_h); vy = -vy; bounced = true; }

        if (bounced) {
            sleep_ticks(ticks_per_step); 

            int digit = pi_digits[pi_index];
            pi_index++;
            if (pi_index >= 30) pi_index = 0; 
            
            float base_variance = (float)digit * 0.03f; 
            float rand_extra = (float)((rand() % 14) + 2); 
            float total_rad = (base_variance + rand_extra) * 0.0174533f;

            rotate_vector(&vx, &vy, total_rad);

            if (fabs(vx) < 1.0f) vx = (vx < 0) ? -1.0f : 1.0f;
            if (fabs(vy) < 1.0f) vy = (vy < 0) ? -1.0f : 1.0f;

            float current_speed = sqrt(vx*vx + vy*vy);
            vx = (vx / current_speed) * speed;
            vy = (vy / current_speed) * speed;

            int new_idx = current_color_idx;
            while (new_idx == current_color_idx) new_idx = rand() % 8;
            current_color_idx = new_idx;
            active_color = dvd_colors[current_color_idx];
        }

        bool should_render = true;
        if (fps_mode == 30 && (frame_counter % 2 != 0)) should_render = false;

        if (should_render) {
            int ix = (int)x;
            int iy = (int)y;

            if (!first_frame) {
                int clear_x = last_render_x - 2;
                int clear_y = last_render_y - 2;
                int clear_w = img_w + 4;
                int clear_h = img_h + 4;
                r->drawRect(clear_x, clear_y, clear_w, clear_h, 0x000000);
            }
            
            r->renderBitmapColored(ix, iy, img_w, img_h, g_dvd_image_data, active_color);
            
            last_render_x = ix;
            last_render_y = iy;
            first_frame = false;
        }

        frame_counter++;
        sleep_ticks(ticks_per_step);
    }

    r->clear(0x000000);
}