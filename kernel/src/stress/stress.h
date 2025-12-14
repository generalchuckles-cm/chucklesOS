#ifndef STRESS_H
#define STRESS_H

#include "../render.h"
#include <cstdint>

// Runs the Ball Stress Test
// override_speed: CPU speed in 100MHz
void run_stress_test(Renderer* r, uint64_t override_speed = 0);

#endif