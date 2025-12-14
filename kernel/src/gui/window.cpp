#include "window.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../input.h"
#include "../globals.h"

// --- Windows 98 Color Palette ---
#define W98_SILVER      0xC0C0C0 // Face
#define W98_WHITE       0xFFFFFF // Highlight
#define W98_GRAY        0x808080 // Shadow
#define W98_BLACK       0x000000 // Dark Shadow
#define W98_TITLE_L     0x000080 // Navy (Active Left)
#define W98_TITLE_R     0x1084D0 // Royal Blue (Active Right)
#define W98_INACTIVE    0x808080 // Dark Gray (Inactive Title)

// --- Helper: Draw 3D Bevel (Raised) ---
static void draw_3d_rect(Renderer* r, int x, int y, int w, int h, bool sunken = false) {
    uint32_t c_tl_outer = sunken ? W98_GRAY  : W98_WHITE; // Top-Left Outer
    uint32_t c_tl_inner = sunken ? W98_BLACK : W98_SILVER; // Top-Left Inner (often transparent/face)
    uint32_t c_br_inner = sunken ? W98_SILVER : W98_GRAY;  // Bottom-Right Inner
    uint32_t c_br_outer = sunken ? W98_WHITE : W98_BLACK;  // Bottom-Right Outer

    // Top Edge
    r->drawRect(x, y, w, 1, c_tl_outer);
    r->drawRect(x + 1, y + 1, w - 2, 1, c_tl_inner); // Inner padding usually just face color for buttons, but specific for window borders

    // Left Edge
    r->drawRect(x, y, 1, h, c_tl_outer);
    r->drawRect(x + 1, y + 1, 1, h - 2, c_tl_inner);

    // Right Edge
    r->drawRect(x + w - 1, y, 1, h, c_br_outer);
    r->drawRect(x + w - 2, y + 1, 1, h - 2, c_br_inner);

    // Bottom Edge
    r->drawRect(x, y + h - 1, w, 1, c_br_outer);
    r->drawRect(x + 1, y + h - 2, w - 2, 1, c_br_inner);
}

// --- Window Implementation ---

Window::Window(int x, int y, int w, int h, const char* t, WindowApp* a) 
    : x(x), y(y), width(w), height(h), app(a), should_close(false), 
      is_focused(false), is_dragging(false) {
    
    strcpy(title, t);
    
    // 1. Allocate Private Buffer
    backing_buffer = (uint32_t*)malloc(width * height * 4);
    if (backing_buffer) memset(backing_buffer, 0, width * height * 4);
    
    // 2. Create Virtual Framebuffer Struct
    limine_framebuffer* vfb = (limine_framebuffer*)malloc(sizeof(limine_framebuffer));
    vfb->address = backing_buffer;
    vfb->width = width;
    vfb->height = height;
    vfb->pitch = width * 4;
    vfb->bpp = 32;
    
    // 3. Create Renderer and Console for this window
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

    // Geometry calculations
    // In this OS, 'x' and 'y' are the top-left of the CONTENT.
    // The frame is drawn AROUND it.
    int border_thick = 3;
    int title_h = 18;
    int frame_x = x - border_thick;
    int frame_y = y - title_h - border_thick;
    int frame_w = width + (border_thick * 2);
    int frame_h = height + title_h + (border_thick * 2);

    // 1. Draw Main Window Background (The "Frame")
    r->drawRect(frame_x, frame_y, frame_w, frame_h, W98_SILVER);

    // 2. Draw Outer 3D Bevel (Raised Window style)
    // Outer White
    r->drawRect(frame_x, frame_y, frame_w, 1, W98_WHITE); // Top
    r->drawRect(frame_x, frame_y, 1, frame_h, W98_WHITE); // Left
    // Outer Black
    r->drawRect(frame_x, frame_y + frame_h - 1, frame_w, 1, W98_BLACK); // Bottom
    r->drawRect(frame_x + frame_w - 1, frame_y, 1, frame_h, W98_BLACK); // Right
    // Inner Gray (Shadow)
    r->drawRect(frame_x + 1, frame_y + frame_h - 2, frame_w - 2, 1, W98_GRAY); // Bottom Inner
    r->drawRect(frame_x + frame_w - 2, frame_y + 1, 1, frame_h - 2, W98_GRAY); // Right Inner
    // Inner Silver/White (Highlight) - usually just Silver face, but let's conform to standard raised
    r->drawRect(frame_x + 1, frame_y + 1, frame_w - 3, 1, W98_SILVER); // Top Inner
    r->drawRect(frame_x + 1, frame_y + 1, 1, frame_h - 3, W98_SILVER); // Left Inner

    // 3. Draw Title Bar
    // The title bar is inside the frame, with a small padding
    int title_x = frame_x + 3;
    int title_y = frame_y + 3;
    int title_w = frame_w - 6;
    
    // Colors
    uint32_t c1 = is_focused ? W98_TITLE_L : W98_INACTIVE;
    uint32_t c2 = is_focused ? W98_TITLE_R : W98_INACTIVE;

    int r1 = (c1 >> 16) & 0xFF; int g1 = (c1 >> 8) & 0xFF; int b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF; int g2 = (c2 >> 8) & 0xFF; int b2 = c2 & 0xFF;

    // Gradient
    for (int i = 0; i < title_w; i++) {
        int r_val = r1 + ((r2 - r1) * i) / title_w;
        int g_val = g1 + ((g2 - g1) * i) / title_w;
        int b_val = b1 + ((b2 - b1) * i) / title_w;
        uint32_t col = (r_val << 16) | (g_val << 8) | b_val;
        r->drawRect(title_x + i, title_y, 1, title_h, col);
    }

    // 4. Draw Title Text
    // Windows 98 text was usually bold, we simulate bold by drawing 1px offset shadow if needed, 
    // but standard system font is usually fine.
    r->drawString(title_x + 4, title_y + 2, title, W98_WHITE);


// 5. Draw Close Button [X]
int btn_size = 14; // Slightly smaller than title bar height
int btn_x = title_x + title_w - btn_size - 2;
int btn_y = title_y + (title_h - btn_size) / 2; // Vertically center the button in the title bar

// Button Background
r->drawRect(btn_x, btn_y, btn_size, btn_size, W98_SILVER);

// Button Bevel (Raised)
draw_3d_rect(r, btn_x, btn_y, btn_size, btn_size, false);

// Button Icon 'X' (Black, bold-ish)
// Center the X in the button
int char_w = 8;  // Approximate char width
int char_h = 8;  // Approximate char height
int char_x = btn_x + (btn_size - char_w) / 2;
int char_y = btn_y + (btn_size - char_h) / 2 - 5;

// Draw X with simple bold effect
r->drawChar(char_x, char_y, 'X', W98_BLACK);
r->drawChar(char_x + 1, char_y, 'X', W98_BLACK); // bold-ish
r->drawChar(char_x, char_y + 1, 'X', W98_BLACK); // optional slight vertical bold


    // 6. Blit Content
    // We render the content *sunken* (inset)
    // Draw a dark border around the content area
    // Top/Left Gray/Black, Bottom/Right White/Silver
    
    // Note: 'x' and 'y' passed to renderBitmap32 are screen coordinates for the backing buffer
    // The backing buffer represents the client area.
    
    // Draw Sunken border around content
    r->drawRect(x - 1, y - 1, width + 2, 1, W98_GRAY);  // Top
    r->drawRect(x - 1, y - 1, 1, height + 2, W98_GRAY); // Left
    r->drawRect(x - 1, y + height, width + 2, 1, W98_WHITE); // Bottom
    r->drawRect(x + width, y - 1, 1, height + 2, W98_WHITE); // Right
    
    // Actually render the buffer
    r->renderBitmap32(x, y, width, height, backing_buffer);
}

void Window::handle_mouse(int mx, int my, bool left) {
    // Adjust hit testing to match the new visual geometry
    // Title bar is roughly from (y - 21) to (y - 3)
    // Close button is at right side
    
    if (left && !is_dragging) {
        // Hit test title bar area
        // Expanded slightly for usability
        if (mx >= x && mx <= x + width && my >= y - 22 && my <= y) {
            
            // Hit test close button
            // Button is on the far right
            int btn_size = 16;
            int btn_start_x = x + width - btn_size - 4; // Approx based on render logic
            
            if (mx >= btn_start_x) {
                should_close = true;
                return;
            }
            
            // Start Drag
            is_dragging = true;
            drag_offset_x = mx - x;
            drag_offset_y = my - y;
        }
    }
    
    if (!left) {
        is_dragging = false;
    }
    
    if (is_dragging) {
        x = mx - drag_offset_x;
        y = my - drag_offset_y;
    }
}

void Window::handle_keyboard(char c) {
    if (app) app->on_input(c);
}

// --- Window Manager Implementation ---

const uint8_t WindowManager::cursor_bitmap[] = {
    1,1,0,0,0,0,0,0,0,0,0,0,
    1,2,1,0,0,0,0,0,0,0,0,0,
    1,2,2,1,0,0,0,0,0,0,0,0,
    1,2,2,2,1,0,0,0,0,0,0,0,
    1,2,2,2,2,1,0,0,0,0,0,0,
    1,2,2,2,2,2,1,0,0,0,0,0,
    1,2,2,2,2,2,2,1,0,0,0,0,
    1,2,2,2,2,2,2,2,1,0,0,0,
    1,2,2,2,2,2,2,2,2,1,0,0,
    1,2,2,2,2,2,2,2,2,2,1,0,
    1,2,2,2,2,2,1,1,1,1,1,1,
    1,2,2,2,1,2,1,0,0,0,0,0,
    1,2,2,1,0,1,2,1,0,0,0,0,
    1,2,1,0,0,1,2,1,0,0,0,0,
    1,1,0,0,0,0,1,2,1,0,0,0,
    0,0,0,0,0,0,1,2,1,0,0,0,
    0,0,0,0,0,0,0,1,1,0,0,0
};

WindowManager& WindowManager::getInstance() {
    static WindowManager instance;
    return instance;
}

WindowManager::WindowManager() : window_count(0), focused_index(-1), backbuffer(nullptr) {}

void WindowManager::init(int w, int h) {
    window_count = 0;
    focused_index = -1;
    screen_width = w;
    screen_height = h;
    
    // Allocate Double Buffer
    backbuffer = (uint32_t*)malloc(w * h * 4);
    if (!backbuffer) {
        // Panic or handle error
    }
    // Set background to a Windows 98 Teal/Green-ish desktop color
    // Standard Win98 background was #008080 (Teal)
    memset(backbuffer, 0, w * h * 4); 

    if (g_renderer) {
        g_mouse_x = w / 2;
        g_mouse_y = h / 2;
    }
}

void WindowManager::add_window(Window* win) {
    if (window_count < MAX_WINDOWS) {
        windows[window_count++] = win;
        focused_index = window_count - 1;
    }
}

Console* WindowManager::get_focused_console() {
    if (focused_index >= 0 && focused_index < window_count) {
        return windows[focused_index]->console;
    }
    return nullptr;
}

void WindowManager::update() {
    bool left = g_mouse_left;
    
    // Focus Logic
    if (left && !last_mouse_left) {
        for (int i = window_count - 1; i >= 0; i--) {
            Window* w = windows[i];
            // Hit test using the new Windows 98 geometry constraints
            // Content is at x,y. Frame extends up by ~22px and around by 3px
            if (g_mouse_x >= w->x - 3 && g_mouse_x <= w->x + w->width + 3 &&
                g_mouse_y >= w->y - 22 && g_mouse_y <= w->y + w->height + 3) {
                
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

    if (focused_index >= 0) {
        windows[focused_index]->handle_mouse(g_mouse_x, g_mouse_y, left);
    }
    
    if (focused_index >= 0 && windows[focused_index]->should_close) {
        Window* dead = windows[focused_index];
        for (int i = focused_index; i < window_count - 1; i++) {
            windows[i] = windows[i+1];
        }
        window_count--;
        if (window_count > 0) focused_index = window_count - 1;
        else focused_index = -1;
        
        delete dead;
    }

    last_mouse_left = left;

    char c = input_check_char();
    if (c != 0) {
        if (focused_index >= 0) {
            windows[focused_index]->handle_keyboard(c);
        }
    }
}

void WindowManager::render(Renderer* global_renderer) {
    if (!backbuffer) return;

    // 1. Create a temporary Renderer bound to the Backbuffer
    limine_framebuffer virtual_fb = {
        .address = backbuffer,
        .width = (uint64_t)screen_width,
        .height = (uint64_t)screen_height,
        .pitch = (uint64_t)screen_width * 4,
        .bpp = 32
    };
    extern const uint8_t g_zap_font[];
    Renderer offscreen(&virtual_fb, g_zap_font);

    // 2. Clear Backbuffer to Windows 98 Teal
    offscreen.clear(0x008080); // Classic Desktop Color
    
    // 3. Draw Windows to Backbuffer
    for (int i = 0; i < window_count; i++) {
        windows[i]->render_frame(&offscreen);
    }
    
    // 4. Draw Mouse Cursor to Backbuffer
    int mx = g_mouse_x;
    int my = g_mouse_y;
    
    for (int y = 0; y < CURSOR_H; y++) {
        if (my + y >= screen_height) break;
        for (int x = 0; x < CURSOR_W; x++) {
            if (mx + x >= screen_width) break;
            
            uint8_t p = cursor_bitmap[y * CURSOR_W + x];
            if (p == 1) backbuffer[(my + y) * screen_width + (mx + x)] = 0x000000;
            else if (p == 2) backbuffer[(my + y) * screen_width + (mx + x)] = 0xFFFFFF;
        }
    }

    // 5. Flip Buffer (Copy Backbuffer to VRAM)
    global_renderer->renderBitmap32(0, 0, screen_width, screen_height, backbuffer);
}