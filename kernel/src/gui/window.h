#ifndef WINDOW_H
#define WINDOW_H

#include <cstdint>
#include "../render.h"
#include "../console.h"

class Window;

// Abstract base class for applications running in a window
class WindowApp {
public:
    virtual void on_init(Window* win) = 0;
    virtual void on_draw() = 0;
    virtual void on_input(char c) = 0;
    virtual ~WindowApp() {}
};

class Window {
public:
    int x, y;
    int width, height;
    char title[64];
    
    uint32_t* backing_buffer; // The window's private video memory
    Renderer* renderer;       // A renderer bound to the private buffer
    Console* console;         // A console bound to this window
    
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
    
    // Init now takes screen dims to allocate backbuffer
    void init(int screen_w, int screen_h);
    
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
    
    // Double Buffering
    uint32_t* backbuffer;
    int screen_width;
    int screen_height;
    
    // Mouse State Tracking
    int last_mouse_x, last_mouse_y;
    bool last_mouse_left;
    
    // Cursor Bitmap (Simple Arrow)
    static const int CURSOR_W = 12;
    static const int CURSOR_H = 17;
    static const uint8_t cursor_bitmap[];
};

#endif