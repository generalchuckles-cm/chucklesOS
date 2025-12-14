#include "string.h"

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* saved = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = 0;
    return saved;
}

char* strcat(char* dest, const char* src) {
    char* ptr = dest + strlen(dest);
    while (*src) {
        *ptr++ = *src++;
    }
    *ptr = 0;
    return dest;
}

const char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return haystack; 

    const char* h = haystack;
    while (*h) {
        const char* h_runner = h;
        const char* n_runner = needle;

        while (*h_runner && *n_runner && *h_runner == *n_runner) {
            h_runner++;
            n_runner++;
        }

        if (!*n_runner) {
            return h; 
        }

        h++; 
    }

    return nullptr; 
}

// Memory operations
void* memcpy(void* __restrict dest, const void* __restrict src, size_t n) {
    uint8_t* __restrict pdest = static_cast<uint8_t* __restrict>(dest);
    const uint8_t* __restrict psrc = static_cast<const uint8_t* __restrict>(src);
    for (size_t i = 0; i < n; i++) pdest[i] = psrc[i];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(s);
    for (size_t i = 0; i < n; i++) p[i] = static_cast<uint8_t>(c);
    return s;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* pdest = static_cast<uint8_t*>(dest);
    const uint8_t* psrc = static_cast<const uint8_t*>(src);
    if (src > dest) {
        for (size_t i = 0; i < n; i++) pdest[i] = psrc[i];
    } else if (src < dest) {
        for (size_t i = n; i > 0; i--) pdest[i-1] = psrc[i-1];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = static_cast<const uint8_t*>(s1);
    const uint8_t* p2 = static_cast<const uint8_t*>(s2);
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] < p2[i] ? -1 : 1;
    }
    return 0;
}