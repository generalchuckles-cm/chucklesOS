#include "window.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../input.h"
#include "../globals.h"
#include "system_widget.h" 

// --- Default Theme Initialization (Dark Mode) ---
Theme g_theme = { 
    0x303030, // win_bg
    0x505050, // border_light
    0x101010, // border_dark
    0x000040, // title_active_1
    0x000080, // title_active_2
    0x404040, // title_inactive
    0xFFFFFF, // text_color
    0x404040, // btn_face
    false     // flat
};

Window::Window(int x, int y, int w, int h, const char* t, WindowApp* a) 
    : x(x), y(y), width(w), height(h), app(a), should_close(false), 
      is_focused(false), is_dragging(false) {
    strcpy(title, t);
    backing_buffer = (uint32_t*)malloc(width * height * 4);
    if (backing_buffer) memset(backing_buffer, 0, width * height * 4);
    limine_framebuffer* vfb = (limine_framebuffer*)malloc(sizeof(limine_framebuffer));
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
    if (console) delete console;
    if (renderer) delete renderer;
    if (backing_buffer) free(backing_buffer);
    if (app) delete app;
}

void Window::render_frame(Renderer* r) {
    if (!backing_buffer) return;
    if (app) app->on_draw();

    int border = g_theme.flat ? 1 : 3;
    int title_h = 18;
    int fx = x - border;
    int fy = y - title_h - border;
    int fw = width + (border * 2);
    int fh = height + title_h + (border * 2);

    r->drawRect(fx, fy, fw, fh, g_theme.win_bg);
    
    if (!g_theme.flat) {
        // 3D Bevels
        r->drawRect(fx, fy, fw, 1, g_theme.border_light); 
        r->drawRect(fx, fy, 1, fh, g_theme.border_light);
        r->drawRect(fx, fy+fh-1, fw, 1, g_theme.border_dark);
        r->drawRect(fx+fw-1, fy, 1, fh, g_theme.border_dark);
        // Inner Shadow
        r->drawRect(fx+1, fy+fh-2, fw-2, 1, g_theme.border_dark);
        r->drawRect(fx+fw-2, fy+1, 1, fh-2, g_theme.border_dark);
    } else {
        // Simple Border
        r->drawRect(fx, fy, fw, 1, g_theme.border_dark);
        r->drawRect(fx, fy+fh-1, fw, 1, g_theme.border_dark);
        r->drawRect(fx, fy, 1, fh, g_theme.border_dark);
        r->drawRect(fx+fw-1, fy, 1, fh, g_theme.border_dark);
    }

    // Title Bar Gradient
    int tx = fx + border; int ty = fy + border; int tw = fw - (border*2);
    
    if (is_focused) {
        int r1=(g_theme.title_active_1>>16)&0xFF, g1=(g_theme.title_active_1>>8)&0xFF, b1=g_theme.title_active_1&0xFF;
        int r2=(g_theme.title_active_2>>16)&0xFF, g2=(g_theme.title_active_2>>8)&0xFF, b2=g_theme.title_active_2&0xFF;
        
        for(int i=0; i<tw; i++) {
            int nr = r1 + ((r2-r1)*i)/tw;
            int ng = g1 + ((g2-g1)*i)/tw;
            int nb = b1 + ((b2-b1)*i)/tw;
            r->drawRect(tx+i, ty, 1, title_h, (nr<<16)|(ng<<8)|nb);
        }
    } else {
        r->drawRect(tx, ty, tw, title_h, g_theme.title_inactive);
    }
    
    r->drawString(tx+4, ty+2, title, g_theme.text_color);

    // Close Button
    int bs = 14;
    int bx = tx + tw - bs - 2;
    int by = ty + 2;
    
    r->drawRect(bx, by, bs, bs, g_theme.btn_face);
    
    if (!g_theme.flat) {
        r->drawRect(bx, by, bs, 1, g_theme.border_light);
        r->drawRect(bx, by, 1, bs, g_theme.border_light);
        r->drawRect(bx, by+bs-1, bs, 1, g_theme.border_dark);
        r->drawRect(bx+bs-1, by, 1, bs, g_theme.border_dark);
    } else {
        r->drawRect(bx, by, bs, 1, g_theme.border_dark);
        r->drawRect(bx, by+bs-1, bs, 1, g_theme.border_dark);
        r->drawRect(bx, by, 1, bs, g_theme.border_dark);
        r->drawRect(bx+bs-1, by, 1, bs, g_theme.border_dark);
    }

    // X Icon
    int cx = bx + bs/2; int cy = by + bs/2;
    uint32_t xcol = g_theme.text_color;
    // Invert X for contrast if needed? Assume text color works.
    if (g_theme.btn_face == 0x000000) xcol = 0xFFFFFF; // Fix for mono theme
    else xcol = 0x000000; // Default black X usually looks best on buttons unless button is black

    r->drawLine(cx-3, cy-3, cx+3, cy+3, xcol);
    r->drawLine(cx-2, cy-3, cx+4, cy+3, xcol); 
    r->drawLine(cx+3, cy-3, cx-3, cy+3, xcol);
    r->drawLine(cx+4, cy-3, cx-2, cy+3, xcol); 

    r->renderBitmap32(x, y, width, height, backing_buffer);
}

void Window::handle_mouse(int mx, int my, bool left) {
    int border = g_theme.flat ? 1 : 3;
    int title_h = 18;
    
    if (left && !is_dragging) {
        if (mx >= x - border && mx <= x + width + border &&
            my >= y - title_h - border && my <= y) {
            
            // Hitbox for X button is top right
            int btn_size = 16; 
            int btn_start = x + width + border - btn_size - 4; // Approx
            
            if (mx >= btn_start) {
                should_close = true;
                return;
            }

            is_dragging = true;
            drag_offset_x = mx - x;
            drag_offset_y = my - y;
        }
        
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

const uint8_t WindowManager::cursor_bitmap[] = {
    1,1,0,0,0,0,0,0,0,0,0,0, 1,2,1,0,0,0,0,0,0,0,0,0, 1,2,2,1,0,0,0,0,0,0,0,0,
    1,2,2,2,1,0,0,0,0,0,0,0, 1,2,2,2,2,1,0,0,0,0,0,0, 1,2,2,2,2,2,1,0,0,0,0,0,
    1,2,2,2,2,2,2,1,0,0,0,0, 1,2,2,2,2,2,2,2,1,0,0,0, 1,2,2,2,2,2,2,2,2,1,0,0,
    1,2,2,2,2,2,2,2,2,2,1,0, 1,2,2,2,2,2,1,1,1,1,1,1, 1,2,2,2,1,2,1,0,0,0,0,0,
    1,2,2,1,0,1,2,1,0,0,0,0, 1,2,1,0,0,1,2,1,0,0,0,0, 1,1,0,0,0,0,1,2,1,0,0,0,
    0,0,0,0,0,0,1,2,1,0,0,0, 0,0,0,0,0,0,0,1,1,0,0,0
};

WindowManager& WindowManager::getInstance() {
    static WindowManager instance;
    return instance;
}

WindowManager::WindowManager() : 
    window_count(0), focused_index(-1), 
    physical_backbuffer(nullptr), logical_buffer(nullptr) {}

void WindowManager::init(int w, int h) {
    printf("WM: Init (%dx%d). Allocating buffers...\n", w, h);
    physical_width = w; physical_height = h;
    logical_width = w; logical_height = h;
    
    physical_backbuffer = (uint32_t*)malloc(w * h * 4);
    logical_buffer = (uint32_t*)malloc(w * h * 4);
    
    if (!physical_backbuffer || !logical_buffer) {
        printf("WM: PANIC - OOM!\n");
        while(1) asm("hlt");
    }
    
    for(int i=0; i<w*h; i++) physical_backbuffer[i] = 0x008080;
    
    if (g_renderer) { g_mouse_x = w / 2; g_mouse_y = h / 2; }
    printf("WM: Init Complete.\n");
}

void WindowManager::reallocate_buffers() {
    if (logical_buffer) free(logical_buffer);
    if (g_display_settings.target_width == 0) {
        logical_width = physical_width; logical_height = physical_height;
    } else {
        logical_width = g_display_settings.target_width; logical_height = g_display_settings.target_height;
    }
    logical_buffer = (uint32_t*)malloc(logical_width * logical_height * 4);
    if (!logical_buffer) return;
    memset(logical_buffer, 0, logical_width * logical_height * 4);
    g_display_settings.dirty = false;
    g_mouse_x = logical_width / 2; g_mouse_y = logical_height / 2;
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
    if (left && !last_mouse_left) {
        for (int i = window_count - 1; i >= 0; i--) {
            Window* w = windows[i];
            int border = g_theme.flat ? 1 : 3;
            int title = 18;
            if (g_mouse_x >= w->x - border && g_mouse_x <= w->x + w->width + border &&
                g_mouse_y >= w->y - title - border && g_mouse_y <= w->y + w->height + border) {
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
    for(int i=0; i<window_count; i++) windows[i]->is_focused = (i == focused_index);
    if (focused_index >= 0) windows[focused_index]->handle_mouse(g_mouse_x, g_mouse_y, left);
    if (focused_index >= 0 && windows[focused_index]->should_close) {
        Window* dead = windows[focused_index];
        for (int i = focused_index; i < window_count - 1; i++) windows[i] = windows[i+1];
        window_count--;
        if (window_count > 0) focused_index = window_count - 1; else focused_index = -1;
        delete dead;
    }
    last_mouse_left = left;
    char c = input_check_char();
    if (c != 0 && focused_index >= 0) windows[focused_index]->handle_keyboard(c);
}

uint32_t WindowManager::process_pixel(uint32_t c) {
    uint8_t r = (c >> 16) & 0xFF; uint8_t g = (c >> 8) & 0xFF; uint8_t b = c & 0xFF;
    switch (g_display_settings.mode) {
        case MODE_GRAYSCALE: { uint8_t l = (r+g+b)/3; return (l<<16)|(l<<8)|l; }
        case MODE_MONOCHROME: { uint8_t l = (r+g+b)/3; return (l>128)?0xFFFFFF:0; }
        case MODE_4COLOR: { 
            int dk=r*r+g*g+b*b; int dw=(255-r)*(255-r)+(255-g)*(255-g)+(255-b)*(255-b);
            int dc=r*r+(255-g)*(255-g)+(255-b)*(255-b); int dm=(255-r)*(255-r)+g*g+(255-b)*(255-b);
            int md=dk; uint32_t mc=0;
            if(dw<md){md=dw;mc=0xFFFFFF;} if(dc<md){md=dc;mc=0x00FFFF;} if(dm<md){md=dm;mc=0xFF00FF;}
            return mc;
        }
        case MODE_8BIT: { return ((r&0xE0)<<16)|((g&0xE0)<<8)|(b&0xC0); }
        case MODE_16BIT: { return ((r&0xF8)<<16)|((g&0xFC)<<8)|(b&0xF8); }
        case MODE_24BIT: { return c & 0xFFFFFF; }
        default: return c;
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

    offscreen.clear(g_theme.win_bg); // Use theme BG for desktop background? Or standard teal?
    // Actually, Windows usually had a specific desktop color (Teal 008080).
    // Let's stick to Teal for desktop, keep win_bg for windows.
    offscreen.clear(0x008080);
    
    for (int i = 0; i < window_count; i++) windows[i]->render_frame(&offscreen);
    SystemWidget::render(&offscreen, logical_width);
    SystemWidget::render_process_list(&offscreen, logical_width, windows, window_count);
    
    int mx = g_mouse_x; int my = g_mouse_y;
    for (int y = 0; y < CURSOR_H; y++) {
        if (my + y >= logical_height) break;
        for (int x = 0; x < CURSOR_W; x++) {
            if (mx + x >= logical_width) break;
            uint8_t p = cursor_bitmap[y * CURSOR_W + x];
            if (p == 1) logical_buffer[(my + y) * logical_width + (mx + x)] = 0;
            else if (p == 2) logical_buffer[(my + y) * logical_width + (mx + x)] = 0xFFFFFF;
        }
    }

    float scale_x = (float)physical_width / (float)logical_width;
    float scale_y = (float)physical_height / (float)logical_height;
    
    if (logical_width == physical_width && logical_height == physical_height) {
        for (int i=0; i<physical_width*physical_height; i++) {
            physical_backbuffer[i] = process_pixel(logical_buffer[i]);
        }
    } else {
        for (int y = 0; y < physical_height; y++) {
            int src_y = (int)(y / scale_y);
            if (src_y >= logical_height) src_y = logical_height - 1;
            for (int x = 0; x < physical_width; x++) {
                int src_x = (int)(x / scale_x);
                if (src_x >= logical_width) src_x = logical_width - 1;
                physical_backbuffer[y * physical_width + x] = process_pixel(logical_buffer[src_y * logical_width + src_x]);
            }
        }
    }

    global_renderer->renderBitmap32(0, 0, physical_width, physical_height, physical_backbuffer);
}