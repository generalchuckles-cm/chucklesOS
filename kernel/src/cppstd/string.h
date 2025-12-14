#ifndef STRING_H
#define STRING_H

#include <cstddef>
#include <cstdint>

// C-compatible functions are inside the extern "C" block
#ifdef __cplusplus
extern "C" {
#endif

int strcmp(const char* s1, const char* s2);
size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
char* strcat(char* dest, const char* src);

void* memcpy(void* __restrict dest, const void* __restrict src, size_t n);
void* memset(void* s, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

// Declare the primary const version for C compatibility
const char* strstr(const char* haystack, const char* needle);

#ifdef __cplusplus
}
#endif

// C++ specific overloads go outside the extern "C" block
#ifdef __cplusplus
inline char* strstr(char* haystack, const char* needle) {
    return (char*)strstr((const char*)haystack, needle);
}
#endif

#endif