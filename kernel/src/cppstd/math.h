#ifndef MATH_H
#define MATH_H

#define PI 3.14159265359f

float fabs(float x);
float sqrt(float x);
float sin(float x);
float cos(float x);

// Arc Tangent 2 (returns angle in radians between -PI and PI)
float atan2(float y, float x);

// New: Power, Exponent, and Natural Logarithm functions
float exp(float x);
float log(float x);
// FIX: The parameter name 'exp' has been changed to 'exponent' to avoid hiding the exp() function.
float pow(float base, float exponent);


#endif