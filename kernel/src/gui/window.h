#ifndef WINDOW_H
#define WINDOW_H

#include <cstdint>
#include "../render.h"
#include "../console.h"

// Forward declaration allows WindowApp to use Window*
class Window;

// Abstract base class for applications
class WindowApp {
public:
    virtual void on_init(Window* win) = 0;
    virtual void on_draw() = 0;
    virtual void on_input(char c) = 0;
    
    // Handle mouse clicks relative to window content area
    // Default implementation does nothing (optional for apps)
    virtual void on_mouse(int rel_x, int rel_y, bool left) {
        (void)rel_x;
        (void)rel_y;
        (void)left;
    }
    
    virtual ~WindowApp() {}
};

class Window {
public:
    int x, y;
    int width, height;
    char title[64];
    
    uint32_t* backing_buffer; 
    Renderer* renderer;       
    Console* console;         
    
    WindowApp* app;
    bool should_close;
    bool is_focused;
    bool is_dragging;
    int drag_offset_x, drag_offset_y;

    Window(int x, int y, int w, int h, const char* title, WindowApp* app);
    ~Window();

    void render_frame(Renderer* target_renderer);
    void handle_mouse(int mx, int my, bool left_click);
    void handle_keyboard(char c);
};

class WindowManager {
public:
    static WindowManager& getInstance();
    
    // Init with PHYSICAL screen dims
    void init(int phys_w, int phys_h);
    
    void add_window(Window* win);
    void update();
    void render(Renderer* global_renderer);
    
    Console* get_focused_console();

private:
    WindowManager();
    
    static const int MAX_WINDOWS = 16;
    Window* windows[MAX_WINDOWS];
    int window_count;
    int focused_index;
    
    // Physical Screen Properties (Real Hardware)
    int physical_width;
    int physical_height;
    uint32_t* physical_backbuffer; // The buffer we upscale TO

    // Logical Screen Properties (Virtual Desktop)
    int logical_width;
    int logical_height;
    uint32_t* logical_buffer; // The buffer we render windows INTO
    
    // Mouse Tracking
    bool last_mouse_left;
    
    // Cursor
    static const int CURSOR_W = 12;
    static const int CURSOR_H = 17;
    static const uint8_t cursor_bitmap[];
    
    // Helper to resize logical buffer
    void reallocate_buffers();
    // Helper to process colors
    uint32_t process_pixel(uint32_t c);
};

#endif