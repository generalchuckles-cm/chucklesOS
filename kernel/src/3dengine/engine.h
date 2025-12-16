#ifndef ENGINE_H
#define ENGINE_H

#include "../gui/window.h"

struct Vec3 { float x, y, z; };
struct Point2D { int x, y; bool valid; };

class Engine3DApp : public WindowApp {
public:
    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;

private:
    Window* my_window;
    
    // Scene State
    float rotY;
    float rotX;
    
    // Mesh Data (Static allocation for simplicity)
    static const int NUM_RINGS = 12;
    static const int SEGMENTS_PER_RING = 16;
    Vec3 vertices[12 * 16]; 
    Point2D projected[12 * 16];
    
    Vec3 rotate(Vec3 p, float angleY, float angleX);
};

#endif