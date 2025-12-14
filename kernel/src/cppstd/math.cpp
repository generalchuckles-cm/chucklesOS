#include "math.h"
#include <cstdint>

float fabs(float x) {
    if (x < 0.0f) return -x;
    return x;
}

float sqrt(float x) {
    float res;
    asm volatile ("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

static float wrap_angle(float angle) {
    while (angle > PI) angle -= 2.0f * PI;
    while (angle < -PI) angle += 2.0f * PI;
    return angle;
}

float sin(float x) {
    x = wrap_angle(x);
    float x2 = x * x;
    float term = x;
    float sum = x;
    term *= -x2 / 6.0f;
    sum += term;
    term *= -x2 / 20.0f;
    sum += term;
    term *= -x2 / 42.0f;
    sum += term;
    return sum;
}

float cos(float x) {
    return sin(x + (PI / 2.0f));
}

// Fast approximation of atan2
float atan2(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return PI / 2.0f;
        if (y == 0.0f) return 0.0f;
        return -PI / 2.0f;
    }
    
    float res;
    float z = y / x;
    if (fabs(z) < 1.0f) {
        // atan(z) approx z - z^3/3 + z^5/5
        float z2 = z * z;
        res = z / (1.0f + 0.28f * z2); // Simple rational approx
        if (x < 0.0f) {
            if (y < 0.0f) return res - PI;
            return res + PI;
        }
    } else {
        // Use property atan(z) = PI/2 - atan(1/z)
        res = (PI / 2.0f) - (z / (z*z + 0.28f));
        if (y < 0.0f) res -= PI;
    }
    return res;
}

// --- New Math Functions ---

// A simple Taylor series approximation for e^x
float exp(float x) {
    float sum = 1.0f;
    float term = 1.0f;
    // Use 10 terms for a reasonable approximation
    for (int i = 1; i < 10; i++) {
        term *= x / i;
        sum += term;
    }
    return sum;
}

// A simple Taylor series approximation for ln(x) using the argument (x-1)/(x+1)
// This converges for all x > 0.
float log(float x) {
    if (x <= 0) return -1.0f/0.0f; // Represents negative infinity (NaN-like)
    float y = (x - 1.0f) / (x + 1.0f);
    float y2 = y * y;
    float sum = 0.0f;
    float term = y;
    // Use 10 terms (20 iterations) for reasonable approximation
    for (int i = 1; i < 20; i += 2) {
        sum += term / i;
        term *= y2;
    }
    return 2.0f * sum;
}

// Power function, implemented using exp and log: base^exponent = e^(exponent * log(base))
float pow(float base, float exponent) {
    if (base < 0) return 0.0f; // Not handled
    if (base == 0) return 0.0f;
    return exp(exponent * log(base));
}