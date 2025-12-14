#ifndef STDIO_H
#define STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

// The main attraction
void printf(const char* format, ...);

// Write formatted output to a string buffer
int sprintf(char* str, const char* format, ...);

// Helper to put a single character (used by printf)
void putchar(char c);

// Helper to put a string
void puts(const char* str);

#ifdef __cplusplus
}
#endif

#endif