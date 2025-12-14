#include "engine.h"
#include "../cppstd/math.h"
#include "../timer.h"
#include "../input.h"
#include "../cppstd/stdio.h"

struct Vec3 {
    float x, y, z;
};

// 3D Constants
#define NUM_RINGS 12
#define SEGMENTS_PER_RING 16
#define RADIUS 150.0f
#define FOCAL_LENGTH 400.0f

static Vec3 rotate(Vec3 p, float angleY, float angleX) {
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

void run_3d_demo(Renderer* r, uint64_t override_speed_units, uint64_t target_fps) {
    bool running = true;
    
    if (target_fps == 0) target_fps = 1;

    uint64_t cpu_freq;
    bool manual_mode = false;

    if (override_speed_units > 0) {
        cpu_freq = override_speed_units * 100000000;
        manual_mode = true;
    } else {
        cpu_freq = get_cpu_frequency();
    }

    uint64_t ticks_per_frame = cpu_freq / target_fps;

    // Generate Sphere Vertices
    const int total_verts = NUM_RINGS * SEGMENTS_PER_RING;
    Vec3 vertices[total_verts];
    
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

    float rotY = 0.0f;
    float rotX = 0.0f;
    float bounce_time = 0.0f;
    int center_x = r->getWidth() / 2;
    int center_y = r->getHeight() / 2;

    struct Point2D { int x, y; bool valid; };
    Point2D projected[total_verts];

    while (running) {
        // --- INPUT POLLING ---
        char c = input_check_char();
        if (c == 27) running = false; // ESC
        // ---------------------
        
        r->clear(0x000000);

        rotY += 0.05f;
        rotX += 0.03f;
        bounce_time += 0.2f;

        if (rotY > 2.0f * PI) rotY -= 2.0f * PI;
        if (rotX > 2.0f * PI) rotX -= 2.0f * PI;
        if (bounce_time > 2.0f * PI) bounce_time -= 2.0f * PI;

        float y_offset = sin(bounce_time) * 100.0f;

        // Project
        for (int i = 0; i < total_verts; i++) {
            Vec3 v = vertices[i];
            v = rotate(v, rotY, rotX);
            v.y += y_offset;
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

        // Draw
        for (int i = 0; i < NUM_RINGS; i++) {
            for (int j = 0; j < SEGMENTS_PER_RING; j++) {
                int curr_idx = i * SEGMENTS_PER_RING + j;
                int next_idx = i * SEGMENTS_PER_RING + ((j + 1) % SEGMENTS_PER_RING);

                Point2D p1 = projected[curr_idx];
                Point2D p2 = projected[next_idx];

                if (p1.valid && p2.valid) {
                    uint32_t col = (i % 2 == 0) ? 0x00FFFF : 0x00FF00;
                    r->drawLine(p1.x, p1.y, p2.x, p2.y, col);
                }
            }
        }

        char buf[128];
        sprintf(buf, "CPU: %d MHz (%s) | Target: %d FPS", 
            (int)(cpu_freq / 1000000), 
            manual_mode ? "Man" : "Auto", 
            (int)target_fps);
        
        r->drawString(10, 10, "3D Sphere Demo", 0xFFFFFF);
        r->drawString(10, 26, "Press ESC to exit", 0xAAAAAA);
        r->drawString(10, 42, buf, 0xFFFF00); 

        sleep_ticks(ticks_per_frame); 
    }

    r->clear(0x000000);
}