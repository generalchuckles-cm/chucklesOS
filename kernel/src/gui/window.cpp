#include "window.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../input.h"
#include "../globals.h"
#include "system_widget.h"

// Modern Slate / Adwaita-inspired Palette
Theme g_theme = {
    0x1E1E24, // win_bg (Deep slate surface)
    0x2E2E38, // border (Subtle hairline)
    0x262630, // title_active (Clean matte header)
    0x1B1B22, // title_inactive
    0x5E60CE, // accent
    0xEDEDF0, // text_color
    0x868798, // text_dim
    0xFF5F56, // btn_close (Flat traffic-light badge)
    0x121216, // desktop_bg (Pitch slate)
    0x18181E  // taskbar_bg
};

Window::Window(int x, int y, int w, int h, const char* t, WindowApp* a) 
    : x(x), y(y), width(w), height(h), app(a), should_close(false), 
      is_focused(false), is_dragging(false) {
    strcpy(title, t);
    backing_buffer = (uint32_t*)malloc(width * height * 4);
    if (backing_buffer) memset(backing_buffer, 0, width * height * 4);

    vfb = (limine_framebuffer*)malloc(sizeof(limine_framebuffer));
    vfb->address = backing_buffer;
    vfb->width = width;
    vfb->height = height;
    vfb->pitch = width * 4;
    vfb->bpp = 32;

    extern const uint8_t g_zap_font[];
    renderer = new Renderer(vfb, g_zap_font);
    console = new Console(renderer);

    if (app) app->on_init(this);
}

Window::~Window() {
    if (g_console == console) g_console = nullptr;
    if (console) delete console;
    if (renderer) delete renderer;
    if (vfb) free(vfb);
    if (backing_buffer) free(backing_buffer);
    if (app) delete app;
}

void Window::render_frame(Renderer* r) {
    if (!backing_buffer) return;
    if (app && ((uint64_t)app >= 0xFFFF800000000000ULL)) app->on_draw();

    int title_h = 24;
    int border = 1;
    int fx = x - border;
    int fy = y - title_h - border;
    int fw = width + (border * 2);
    int fh = height + title_h + (border * 2);

    // 1. Soft Shadow
    r->drawRect(fx + 3, fy + 3, fw, fh, 0x0A0A0E);

    // 2. Window Frame
    uint32_t t_color = is_focused ? g_theme.title_active : g_theme.title_inactive;
    r->drawRect(fx, fy, fw, title_h + border, t_color);

    // 1px Border Outline
    uint32_t b_color = is_focused ? 0x3D3E52 : g_theme.border;
    r->drawRect(fx, fy, fw, 1, b_color);
    r->drawRect(fx, fy + fh - 1, fw, 1, b_color);
    r->drawRect(fx, fy, 1, fh, b_color);
    r->drawRect(fx + fw - 1, fy, 1, fh, b_color);

    // 3. Traffic-Light Window Controls (macOS style)
    int btn_radius = 5;
    int btn_cy = fy + (title_h / 2) + 1;
    int close_cx = fx + 14;
    int min_cx = close_cx + 14;
    int max_cx = min_cx + 14;

    r->drawFilledCircle(close_cx, btn_cy, btn_radius, g_theme.btn_close);
    r->drawFilledCircle(min_cx, btn_cy, btn_radius, 0xFEB62D);
    r->drawFilledCircle(max_cx, btn_cy, btn_radius, 0x28C840);

    // 4. Centered Clean Header Title
    int title_len = strlen(title);
    int title_x = fx + (fw / 2) - (title_len * 4);
    if (title_x < max_cx + 12) title_x = max_cx + 12;

    uint32_t text_col = is_focused ? g_theme.text_color : g_theme.text_dim;
    r->drawString(title_x, fy + 5, title, text_col);

    // 5. Backing Buffer Blit
    r->renderBitmap32(x, y, width, height, backing_buffer);
}

void Window::handle_mouse(int mx, int my, bool left) {
    int title_h = 24;

    if (left && !is_dragging) {
        // Close Button Click (Within close dot radius)
        int close_cx = (x - 1) + 14;
        int btn_cy = (y - title_h - 1) + (title_h / 2) + 1;
        int dx = mx - close_cx;
        int dy = my - btn_cy;

        if (dx * dx + dy * dy <= 64) {
            should_close = true;
            return;
        }

        // Titlebar Drag
        if (mx >= x && mx <= x + width && my >= y - title_h && my < y) {
            is_dragging = true;
            drag_offset_x = mx - x;
            drag_offset_y = my - y;
        }

        // Content Interaction
        if (mx >= x && mx < x + width && my >= y && my < y + height) {
            if (app) app->on_mouse(mx - x, my - y, left);
        }
    }

    if (!left) is_dragging = false;

    if (is_dragging) {
        x = mx - drag_offset_x;
        y = my - drag_offset_y;
    }
}

void Window::handle_keyboard(char c) {
    if (app) app->on_input(c);
}

// Modern Angled Minimal Cursor
const uint8_t WindowManager::cursor_bitmap[] = {
    1,0,0,0,0,0,0,0,0,0,0,0,
    1,1,0,0,0,0,0,0,0,0,0,0,
    1,2,1,0,0,0,0,0,0,0,0,0,
    1,2,2,1,0,0,0,0,0,0,0,0,
    1,2,2,2,1,0,0,0,0,0,0,0,
    1,2,2,2,2,1,0,0,0,0,0,0,
    1,2,2,2,2,2,1,0,0,0,0,0,
    1,2,2,2,2,2,2,1,0,0,0,0,
    1,2,2,2,2,2,2,2,1,0,0,0,
    1,2,2,2,2,2,1,1,1,0,0,0,
    1,2,1,1,2,2,1,0,0,0,0,0,
    1,1,0,0,1,2,2,1,0,0,0,0,
    1,0,0,0,1,2,2,1,0,0,0,0,
    0,0,0,0,0,1,2,2,1,0,0,0,
    0,0,0,0,0,1,2,2,1,0,0,0,
    0,0,0,0,0,0,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0
};

WindowManager& WindowManager::getInstance() {
    static WindowManager instance;
    return instance;
}

WindowManager::WindowManager() :
    window_count(0), focused_index(-1), 
    physical_backbuffer(nullptr), logical_buffer(nullptr) {}

void WindowManager::init(int w, int h) {
    printf("WM: Initializing Modern Shell (%dx%d)...\n", w, h);
    physical_width = w; physical_height = h;

    if (g_display_settings.target_width > 0 && g_display_settings.target_height > 0) {
        logical_width = g_display_settings.target_width;
        logical_height = g_display_settings.target_height;
    } else {
        logical_width = w;
        logical_height = h;
    }

    physical_backbuffer = (uint32_t*)malloc(physical_width * physical_height * 4);
    logical_buffer = (uint32_t*)malloc(logical_width * logical_height * 4);

    if (!physical_backbuffer || !logical_buffer) {
        printf("WM: OOM Fatal during Shell startup.\n");
        while (1) asm("hlt");
    }

    for (int i = 0; i < physical_width * physical_height; i++) {
        physical_backbuffer[i] = g_theme.desktop_bg;
    }

    if (g_renderer) {
        g_mouse_x = logical_width / 2;
        g_mouse_y = logical_height / 2;
    }
}

void WindowManager::reallocate_buffers() {
    if (logical_buffer) free(logical_buffer);
    if (physical_backbuffer) free(physical_backbuffer);

    if (g_display_settings.target_width == 0) {
        logical_width = physical_width;
        logical_height = physical_height;
    } else {
        logical_width = g_display_settings.target_width;
        logical_height = g_display_settings.target_height;
    }

    logical_buffer = (uint32_t*)malloc(logical_width * logical_height * 4);
    physical_backbuffer = (uint32_t*)malloc(physical_width * physical_height * 4);
    if (!logical_buffer || !physical_backbuffer) return;

    memset(logical_buffer, 0, logical_width * logical_height * 4);
    g_display_settings.dirty = false;
    g_mouse_x = logical_width / 2;
    g_mouse_y = logical_height / 2;
}

void WindowManager::add_window(Window* win) {
    if (window_count < MAX_WINDOWS) {
        windows[window_count++] = win;
        focused_index = window_count - 1;
    }
}

Console* WindowManager::get_focused_console() {
    if (focused_index >= 0 && focused_index < window_count) 
        return windows[focused_index]->console;
    return nullptr;
}

void WindowManager::update() {
    if (g_display_settings.dirty) reallocate_buffers();

    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= logical_width) g_mouse_x = logical_width - 1;
    if (g_mouse_y >= logical_height) g_mouse_y = logical_height - 1;

    bool left = g_mouse_left;

    // Focus Switching
    if (left && !last_mouse_left) {
        for (int i = window_count - 1; i >= 0; i--) {
            Window* w = windows[i];
            int title_h = 24;
            if (g_mouse_x >= w->x - 2 && g_mouse_x <= w->x + w->width + 2 &&
                g_mouse_y >= w->y - title_h - 2 && g_mouse_y <= w->y + w->height + 2) {
                focused_index = i;
                if (i != window_count - 1) {
                    Window* temp = windows[window_count - 1];
                    windows[window_count - 1] = w;
                    windows[i] = temp;
                    focused_index = window_count - 1;
                }
                break;
            }
        }
    }

    for (int i = 0; i < window_count; i++) {
        windows[i]->is_focused = (i == focused_index);
    }

    if (focused_index >= 0) {
        windows[focused_index]->handle_mouse(g_mouse_x, g_mouse_y, left);

        if (windows[focused_index]->should_close) {
            Window* dead = windows[focused_index];
            for (int i = focused_index; i < window_count - 1; i++) {
                windows[i] = windows[i + 1];
            }
            window_count--;
            focused_index = (window_count > 0) ? window_count - 1 : -1;
            delete dead;
        }
    }

    last_mouse_left = left;
    char c = input_check_char();
    if (c != 0 && focused_index >= 0) {
        windows[focused_index]->handle_keyboard(c);
    }
}

uint32_t WindowManager::process_pixel(uint32_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;

    switch (g_display_settings.mode) {
        case MODE_GRAYSCALE: {
            uint8_t l = (r * 30 + g * 59 + b * 11) / 100;
            return (l << 16) | (l << 8) | l;
        }
        case MODE_MONOCHROME: {
            uint8_t l = (r + g + b) / 3;
            return (l > 120) ? 0xFFFFFF : 0x000000;
        }
        case MODE_4COLOR: {
            int dk = r*r + g*g + b*b;
            int dw = (255-r)*(255-r) + (255-g)*(255-g) + (255-b)*(255-b);
            int dc = r*r + (255-g)*(255-g) + (255-b)*(255-b);
            int dm = (255-r)*(255-r) + g*g + (255-b)*(255-b);
            int md = dk; uint32_t mc = 0x000000;
            if (dw < md) { md = dw; mc = 0xFFFFFF; }
            if (dc < md) { md = dc; mc = 0x00FFFF; }
            if (dm < md) { md = dm; mc = 0xFF00FF; }
            return mc;
        }
        case MODE_8BIT:   return ((r & 0xE0) << 16) | ((g & 0xE0) << 8) | (b & 0xC0);
        case MODE_16BIT:  return ((r & 0xF8) << 16) | ((g & 0xFC) << 8) | (b & 0xF8);
        case MODE_24BIT:  return c & 0xFFFFFF;
        default:          return c;
    }
}

void WindowManager::render(Renderer* global_renderer) {
    if (!logical_buffer || !physical_backbuffer) return;

    limine_framebuffer virtual_fb = {
        .address = logical_buffer,
        .width = (uint64_t)logical_width,
        .height = (uint64_t)logical_height,
        .pitch = (uint64_t)logical_width * 4,
        .bpp = 32
    };

    extern const uint8_t g_zap_font[];
    Renderer offscreen(&virtual_fb, g_zap_font);

    // 1. Dark Velvet Wallpaper
    offscreen.clear(g_theme.desktop_bg);

    // 2. Render Window Stack
    for (int i = 0; i < window_count; i++) {
        windows[i]->render_frame(&offscreen);
    }

    // 3. System Widget Sidebar
    SystemWidget::render(&offscreen, logical_width);
    SystemWidget::render_process_list(&offscreen, logical_width, windows, window_count);

    // 4. Modern Floating Dock Bar
    int dock_h = 36;
    int dock_w = 340;
    int dock_x = (logical_width - dock_w) / 2;
    int dock_y = logical_height - dock_h - 10;

    offscreen.drawRect(dock_x, dock_y, dock_w, dock_h, g_theme.taskbar_bg);
    offscreen.drawRect(dock_x, dock_y, dock_w, 1, 0x2A2A38);
    offscreen.drawRect(dock_x, dock_y + dock_h - 1, dock_w, 1, 0x101014);
    offscreen.drawRect(dock_x, dock_y, 1, dock_h, 0x2A2A38);
    offscreen.drawRect(dock_x + dock_w - 1, dock_y, 1, dock_h, 0x2A2A38);

    offscreen.drawString(dock_x + 18, dock_y + 11, "ChucklesOS", 0xE0E2EE);
    offscreen.drawRect(dock_x + 110, dock_y + 8, 1, 20, 0x2C2D3E);
    offscreen.drawString(dock_x + 124, dock_y + 11, "term  edit  disp  nes  3drnd", 0x8C8EA0);

    // 5. Draw Sleek Arrow Cursor
    int mx = g_mouse_x;
    int my = g_mouse_y;

    for (int y = 0; y < CURSOR_H; y++) {
        if (my + y >= logical_height) break;
        for (int x = 0; x < CURSOR_W; x++) {
            if (mx + x >= logical_width) break;
            uint8_t p = cursor_bitmap[y * CURSOR_W + x];
            if (p == 1) {
                logical_buffer[(my + y) * logical_width + (mx + x)] = 0x000000;
            } else if (p == 2) {
                logical_buffer[(my + y) * logical_width + (mx + x)] = 0xFFFFFF;
            }
        }
    }

    // 6. Blit & Supersample
    float scale_x = (float)physical_width / (float)logical_width;
    float scale_y = (float)physical_height / (float)logical_height;

    if (logical_width == physical_width && logical_height == physical_height) {
        for (int i = 0; i < physical_width * physical_height; i++) {
            physical_backbuffer[i] = process_pixel(logical_buffer[i]);
        }
    } else {
        for (int y = 0; y < physical_height; y++) {
            int src_y = (int)(y / scale_y);
            if (src_y >= logical_height) src_y = logical_height - 1;
            for (int x = 0; x < physical_width; x++) {
                int src_x = (int)(x / scale_x);
                if (src_x >= logical_width) src_x = logical_width - 1;

                uint32_t c = logical_buffer[src_y * logical_width + src_x];

                if (scale_x < 1.0f || scale_y < 1.0f) {
                    int nx = src_x + 1 < logical_width ? src_x + 1 : src_x;
                    int ny = src_y + 1 < logical_height ? src_y + 1 : src_y;
                    uint32_t c2 = logical_buffer[src_y * logical_width + nx];
                    uint32_t c3 = logical_buffer[ny * logical_width + src_x];
                    uint32_t c4 = logical_buffer[ny * logical_width + nx];

                    uint32_t r = (((c >> 16) & 0xFF) + ((c2 >> 16) & 0xFF) + ((c3 >> 16) & 0xFF) + ((c4 >> 16) & 0xFF)) >> 2;
                    uint32_t g = (((c >> 8) & 0xFF) + ((c2 >> 8) & 0xFF) + ((c3 >> 8) & 0xFF) + ((c4 >> 8) & 0xFF)) >> 2;
                    uint32_t b = ((c & 0xFF) + (c2 & 0xFF) + (c3 & 0xFF) + (c4 & 0xFF)) >> 2;
                    c = (r << 16) | (g << 8) | b;
                }

                physical_backbuffer[y * physical_width + x] = process_pixel(c);
            }
        }
    }

    global_renderer->renderBitmap32(0, 0, physical_width, physical_height, physical_backbuffer);
}
