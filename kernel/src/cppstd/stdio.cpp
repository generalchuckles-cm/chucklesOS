#include "stdio.h"
#include "../globals.h"
#include "../sys/spinlock.h"
#include <stdarg.h>
#include <cstddef>

static Spinlock stdio_lock;

static int itoa(unsigned long long value, char* str, int base) {
    if (base < 2 || base > 36) {
        if (str) *str = '\0';
        return 0;
    }

    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    int len = 0;

    // Safety buffer limit check not strictly needed with 64-bit logic below
    // if (value > 0xFFFFFFFFFFFFFFFFULL - 1) value = 0; 

    if (value == 0) {
        *str++ = '0';
        *str = '\0';
        return 1;
    }

    while (value != 0) {
        int rem = value % base;
        *ptr++ = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        value = value / base;
        len++;
    }
    *ptr = '\0';

    // Reverse string
    ptr--;
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr = *ptr1;
        *ptr1 = tmp_char;
        ptr--;
        ptr1++;
    }
    return len;
}

void putchar(char c) {
    if (g_console) {
        g_console->putChar(c);
    }
}

void puts(const char* str) {
    ScopedLock lock(stdio_lock);
    if (g_console && str) {
        g_console->print(str);
        g_console->putChar('\n');
    }
}

void printf(const char* format, ...) {
    ScopedLock lock(stdio_lock);

    if (!g_console || !format) return;

    va_list args;
    va_start(args, format);

    while (*format) {
        if (*format == '%') {
            format++;
            
            // Skip flags
            while ((*format >= '0' && *format <= '9') || 
                   *format == '.' || *format == 'l' || 
                   *format == 'h' || *format == 'z') {
                format++;
            }

            if (*format == 0) break;

            switch (*format) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    putchar(c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    g_console->print(s ? s : "(null)");
                    break;
                }
                case 'd':
                case 'i': {
                    long long val = va_arg(args, long long);
                    if (val < 0) {
                        putchar('-');
                        val = -val;
                    }
                    char buffer[32];
                    itoa((unsigned long long)val, buffer, 10);
                    g_console->print(buffer);
                    break;
                }
                case 'u': {
                    unsigned long long val = va_arg(args, unsigned long long);
                    char buffer[32];
                    itoa(val, buffer, 10);
                    g_console->print(buffer);
                    break;
                }
                case 'x': 
                case 'p': {
                    unsigned long long val = va_arg(args, unsigned long long);
                    g_console->print("0x");
                    char buffer[32];
                    itoa(val, buffer, 16);
                    g_console->print(buffer);
                    break;
                }
                case '%': putchar('%'); break;
                default: putchar(*format); break;
            }
        } else {
            putchar(*format);
        }
        format++;
    }
    va_end(args);
}

int sprintf(char* str, const char* format, ...) {
    if (!str || !format) return 0;
    
    va_list args;
    va_start(args, format);
    char* start = str;

    while (*format) {
        if (*format == '%') {
            format++;
            while ((*format >= '0' && *format <= '9') || 
                   *format == '.' || *format == 'l' || 
                   *format == 'h' || *format == 'z') {
                format++;
            }
            
            if (*format == 0) break;

            switch (*format) {
                case 'c': {
                    *str++ = (char)va_arg(args, int);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (!s) s = "(null)";
                    while (*s) *str++ = *s++;
                    break;
                }
                case 'd':
                case 'i': {
                    long long val = va_arg(args, long long);
                    if (val < 0) {
                        *str++ = '-';
                        val = -val;
                    }
                    char buffer[32];
                    int len = itoa((unsigned long long)val, buffer, 10);
                    for (int i=0; i<len; i++) *str++ = buffer[i];
                    break;
                }
                case 'u': {
                    unsigned long long val = va_arg(args, unsigned long long);
                    char buffer[32];
                    int len = itoa(val, buffer, 10);
                    for (int i=0; i<len; i++) *str++ = buffer[i];
                    break;
                }
                case 'p': // FIXED: Added case p
                case 'x': {
                    unsigned long long val = va_arg(args, unsigned long long);
                    *str++ = '0'; *str++ = 'x';
                    char buffer[32];
                    int len = itoa(val, buffer, 16);
                    for (int i=0; i<len; i++) *str++ = buffer[i];
                    break;
                }
                case '%': *str++ = '%'; break;
                default: *str++ = *format; break;
            }
        } else {
            *str++ = *format;
        }
        format++;
    }
    *str = 0;
    va_end(args);
    return str - start;
}