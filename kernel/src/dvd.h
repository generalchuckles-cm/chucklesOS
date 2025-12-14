#ifndef DVD_H
#define DVD_H

#include "render.h"
#include <cstdint>

// Runs the DVD Bouncing Logo demo.
// override_speed: CPU speed in 100MHz units (e.g. 34 = 3.4GHz). 0 = auto-detect.
// fps_mode: 60 or 30 (Physics always runs at 60Hz, 30 skips rendering every other frame).
void run_dvd_demo(Renderer* r, uint64_t override_speed = 0, uint64_t fps_mode = 60);

#endif