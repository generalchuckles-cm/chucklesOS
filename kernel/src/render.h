#ifndef RENDER_H
#define RENDER_H

#include <limine.h>
#include <cstdint>
#include "font.h"

class Renderer {
public:
    Renderer(limine_framebuffer* framebuffer, const void* psf_font);

    void putPixel(int x, int y, std::uint32_t color);
    void drawRect(int x, int y, int w, int h, std::uint32_t color);
    void drawLine(int x0, int y0, int x1, int y1, std::uint32_t color);
    void drawFilledCircle(int cx, int cy, int radius, std::uint32_t color);
    void clear(std::uint32_t color);

    // Updated: Uses the loaded PSF font
    void drawChar(int x, int y, char c, std::uint32_t color, int scale = 1);
    void drawString(int x, int y, const char* str, std::uint32_t color, int scale = 1);

    // Renders 24-bit RGB packed data
    void renderBitmap(int x, int y, int w, int h, const std::uint8_t* data);
    
    // NEW: Renders 32-bit ARGB/XRGB data (for Window buffers)
    void renderBitmap32(int x, int y, int w, int h, const std::uint32_t* data);

    void renderBitmapColored(int x, int y, int w, int h, const std::uint8_t* data, std::uint32_t color);

    std::uint32_t getWidth() const { return width; }
    std::uint32_t getHeight() const { return height; }
    
    // New Getters for Console
    int getFontWidth() const { return 8; } // PSF1 is always 8 wide
    int getFontHeight() const { return font_header->charsize; }

    limine_framebuffer* getFramebuffer() { return fb; }

private:
    limine_framebuffer* fb;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
    std::uint32_t bpp;
    
    // Font Data
    PSF1_Header* font_header;
    const uint8_t* glyph_buffer;
};

#endif