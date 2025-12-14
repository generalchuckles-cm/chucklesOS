#ifndef STDLIB_H
#define STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

// Seed the random number generator
void srand(unsigned int seed);

// Get a pseudo-random integer
int rand(void);

#ifdef __cplusplus
}
#endif

#endif