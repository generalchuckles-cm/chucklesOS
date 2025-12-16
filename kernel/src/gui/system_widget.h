#ifndef SYSTEM_WIDGET_H
#define SYSTEM_WIDGET_H

#include <cstdint>

// Forward declarations to prevent circular includes
class Renderer;
class Window;

class SystemWidget {
public:
    static void render(Renderer* r, int screen_w);
    static void render_process_list(Renderer* r, int screen_w, Window** windows, int count);
};

#endif