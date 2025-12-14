#ifndef FONT_H
#define FONT_H

#include <cstdint>

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

struct PSF1_Header {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize; // The height of the character (width is always 8 for PSF1)
} __attribute__((packed));

// The external reference to the file you just converted
extern "C" const uint8_t g_zap_font[];
extern "C" const uint32_t g_zap_font_size;

#endif