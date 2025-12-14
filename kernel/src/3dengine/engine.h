#ifndef ENGINE_H
#define ENGINE_H

#include "../render.h"
#include <cstdint>

// Runs the 3D Sphere Demo indefinitely until ESC is pressed.
// override_speed_units: 
//    0 = Auto-detect using CPUID/RDTSC.
//   >0 = Manual speed in 100MHz increments (e.g., 34 = 3.4GHz).
// target_fps:
//    Target framerate (default 10).
void run_3d_demo(Renderer* r, uint64_t override_speed_units = 0, uint64_t target_fps = 10);

#endif