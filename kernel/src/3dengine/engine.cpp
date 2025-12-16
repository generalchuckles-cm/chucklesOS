#include "engine.h"
#include "../cppstd/math.h"
#include "../cppstd/stdio.h"

#define RADIUS 120.0f 
#define FOCAL_LENGTH 300.0f

Vec3 Engine3DApp::rotate(Vec3 p, float angleY, float angleX) {
    float cosY = cos(angleY);
    float sinY = sin(angleY);
    float x1 = p.x * cosY - p.z * sinY;
    float z1 = p.x * sinY + p.z * cosY;
    float y1 = p.y;

    float cosX = cos(angleX);
    float sinX = sin(angleX);
    float y2 = y1 * cosX - z1 * sinX;
    float z2 = y1 * sinX + z1 * cosX;
    
    return {x1, y2, z2};
}

void Engine3DApp::on_init(Window* win) {
    my_window = win;
    rotY = 0; rotX = 0;

    // Generate Sphere
    int v_idx = 0;
    for (int i = 0; i < NUM_RINGS; i++) {
        float theta = (PI * (i + 1)) / (NUM_RINGS + 1);
        float y = RADIUS * cos(theta);
        float ring_radius = RADIUS * sin(theta);
        
        for (int j = 0; j < SEGMENTS_PER_RING; j++) {
            float phi = (2.0f * PI * j) / SEGMENTS_PER_RING;
            float x = ring_radius * cos(phi);
            float z = ring_radius * sin(phi);
            vertices[v_idx++] = {x, y, z};
        }
    }
}

void Engine3DApp::on_input(char c) {
    (void)c;
}

void Engine3DApp::on_draw() {
    my_window->renderer->clear(0x000000);

    rotY += 0.05f;
    rotX += 0.03f;
    
    if (rotY > 2.0f * PI) rotY -= 2.0f * PI;
    if (rotX > 2.0f * PI) rotX -= 2.0f * PI;
    
    int center_x = my_window->width / 2;
    int center_y = my_window->height / 2;
    int total_verts = NUM_RINGS * SEGMENTS_PER_RING;

    // Project
    for (int i = 0; i < total_verts; i++) {
        Vec3 v = vertices[i];
        v = rotate(v, rotY, rotX);
        v.z += 400.0f; 

        if (v.z > 0) {
            float scale = FOCAL_LENGTH / v.z;
            projected[i].x = center_x + (int)(v.x * scale);
            projected[i].y = center_y + (int)(v.y * scale);
            projected[i].valid = true;
        } else {
            projected[i].valid = false;
        }
    }

    // Draw Wireframe
    for (int i = 0; i < NUM_RINGS; i++) {
        for (int j = 0; j < SEGMENTS_PER_RING; j++) {
            int curr_idx = i * SEGMENTS_PER_RING + j;
            int next_idx = i * SEGMENTS_PER_RING + ((j + 1) % SEGMENTS_PER_RING);

            Point2D p1 = projected[curr_idx];
            Point2D p2 = projected[next_idx];

            if (p1.valid && p2.valid) {
                uint32_t col = (i % 2 == 0) ? 0x00FFFF : 0x00FF00;
                my_window->renderer->drawLine(p1.x, p1.y, p2.x, p2.y, col);
            }
        }
    }
}