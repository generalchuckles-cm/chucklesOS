#include "stress.h"
#include "../timer.h"
#include "../input.h"
#include "../cppstd/stdlib.h"
#include "../cppstd/stdio.h"
#include "../cppstd/math.h" 
#include "../memory/heap.h"
#include "../memory/vmm.h"       
#include "../drv/gpu/intel_gpu.h" 

#define MAX_BALLS 5000
#define CONTAINER_RADIUS 350.0f 
#define GAP_SIZE_RAD 0.8f 
#define BALL_RADIUS 4.0f 

static float* b_x = nullptr;
static float* b_y = nullptr;
static float* b_vx = nullptr;
static float* b_vy = nullptr;
static uint32_t* b_color = nullptr;
static bool* b_active = nullptr;
static GpuRect* gpu_rect_list = nullptr; 

#define WALL_POINTS 1440 
struct Point { int x, y; };
static Point wall_lut[WALL_POINTS];

static int ball_count = 0;

static void spawn_ball(float start_x, float start_y) {
    if (ball_count >= MAX_BALLS) return;
    int i = ball_count;
    float angle = (float)(rand() % 360) * 0.01745f;
    float speed = (float)((rand() % 4) + 4); 
    b_x[i] = start_x;
    b_y[i] = start_y;
    b_vx[i] = cos(angle) * speed;
    b_vy[i] = sin(angle) * speed;
    b_active[i] = true;
    uint32_t r = rand() % 128 + 127;
    uint32_t g = rand() % 128 + 127;
    uint32_t b = rand() % 128 + 127;
    b_color[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    ball_count++;
}

void run_stress_test(Renderer* r, uint64_t override_speed) {
    (void)override_speed; // Unused
    bool running = true;
    ball_count = 0;
    srand((unsigned int)rdtsc_serialized());

    printf("STRESS: Allocating buffers...\n");
    b_x = new float[MAX_BALLS];
    b_y = new float[MAX_BALLS];
    b_vx = new float[MAX_BALLS];
    b_vy = new float[MAX_BALLS];
    b_color = new uint32_t[MAX_BALLS];
    b_active = new bool[MAX_BALLS];
    gpu_rect_list = new GpuRect[MAX_BALLS + WALL_POINTS]; 

    uint64_t cpu_freq = get_cpu_frequency();
    uint64_t ticks_per_physics = cpu_freq / 60;
    
    int cx = r->getWidth() / 2;
    int cy = r->getHeight() / 2;
    uint64_t fb_phys = vmm_virt_to_phys((uint64_t)r->getFramebuffer()->address);

    for (int i = 0; i < WALL_POINTS; i++) {
        float a = (float)i * (2.0f * PI / (float)WALL_POINTS);
        wall_lut[i].x = (int)(cos(a) * CONTAINER_RADIUS);
        wall_lut[i].y = (int)(sin(a) * CONTAINER_RADIUS);
    }
    for(int i=0; i<100; i++) spawn_ball((float)cx, (float)cy);
    
    float container_angle = 0.0f;
    uint64_t accumulator = 0;
    uint64_t last_time = rdtsc_serialized();
    
    while (running) {
        // --- INPUT ---
        char c = input_check_char();
        if (c == 27) running = false;
        // -------------

        uint64_t now = rdtsc_serialized();
        uint64_t delta = now - last_time;
        last_time = now;
        if (delta > cpu_freq / 5) delta = cpu_freq / 5;
        accumulator += delta;

        // 1. PHYSICS
        while (accumulator >= ticks_per_physics) {
            container_angle += 0.02f; 
            if (container_angle > PI) container_angle -= 2*PI;
            int spawn_queue = 0;
            float r_inner_sq = (CONTAINER_RADIUS - BALL_RADIUS) * (CONTAINER_RADIUS - BALL_RADIUS);
            float r_inner = CONTAINER_RADIUS - BALL_RADIUS;

            for (int i = 0; i < ball_count; i++) {
                if (!b_active[i]) continue;
                b_x[i] += b_vx[i];
                b_y[i] += b_vy[i];
                b_vy[i] += 0.15f; 
                float dx = b_x[i] - cx;
                float dy = b_y[i] - cy;
                float dist_sq = dx*dx + dy*dy;
                if (dist_sq >= r_inner_sq) {
                    float dist = sqrt(dist_sq);
                    float angle = atan2(dy, dx);
                    float diff = angle - container_angle;
                    while (diff > PI) diff -= 2*PI;
                    while (diff < -PI) diff += 2*PI;
                    if (fabs(diff) < GAP_SIZE_RAD / 2.0f) {
                        b_active[i] = false;
                        spawn_queue += 2;
                    } else {
                        float nx = dx / dist;
                        float ny = dy / dist;
                        float dot = b_vx[i] * nx + b_vy[i] * ny;
                        b_vx[i] = b_vx[i] - 2.0f * dot * nx;
                        b_vy[i] = b_vy[i] - 2.0f * dot * ny;
                        b_vx[i] *= 1.02f; b_vy[i] *= 1.02f;
                        float overlap = dist - r_inner;
                        b_x[i] -= nx * overlap; b_y[i] -= ny * overlap;
                    }
                }
            }
            if (spawn_queue > 200) spawn_queue = 200; 
            for (int k = 0; k < spawn_queue; k++) spawn_ball((float)cx, (float)cy);
            accumulator -= ticks_per_physics;
        }

        // 2. RENDER LIST
        int render_count = 0;
        gpu_rect_list[render_count++] = {0, 0, (int)r->getWidth(), (int)r->getHeight(), 0x000000};

        int rotation_offset_idx = (int)((container_angle / (2.0f * PI)) * WALL_POINTS);
        if (rotation_offset_idx < 0) rotation_offset_idx += WALL_POINTS;
        int gap_half_width = (int)((GAP_SIZE_RAD / (2.0f * PI)) * WALL_POINTS) / 2;
        for (int i = 0; i < WALL_POINTS; i++) {
            int adjusted_idx = (i - rotation_offset_idx);
            if (adjusted_idx < 0) adjusted_idx += WALL_POINTS;
            if (adjusted_idx >= WALL_POINTS) adjusted_idx -= WALL_POINTS;
            if (adjusted_idx >= gap_half_width && adjusted_idx <= WALL_POINTS - gap_half_width) {
                gpu_rect_list[render_count++] = {
                    cx + wall_lut[i].x, cy + wall_lut[i].y, 
                    2, 2, 0xFFFFFF 
                };
            }
        }

        for (int i = 0; i < ball_count; i++) {
            if (b_active[i]) {
                int radius = (int)BALL_RADIUS;
                gpu_rect_list[render_count++] = {
                    (int)b_x[i] - radius, (int)b_y[i] - radius, 
                    radius*2, radius*2, b_color[i]
                };
            }
        }

        g_intel_gpu.render_rect_list(fb_phys, r->getFramebuffer()->pitch, gpu_rect_list, render_count);
    }

    delete[] b_x;
    delete[] b_y;
    delete[] b_vx;
    delete[] b_vy;
    delete[] b_color;
    delete[] b_active;
    delete[] gpu_rect_list;
    r->clear(0x000000);
}