#include "render.h"
#include "cppstd/math.h"
#include "cppstd/sse.h"
#include "cppstd/stdio.h"

static int iabs(int v) { return v < 0 ? -v : v; }

Renderer::Renderer(limine_framebuffer* framebuffer, const void* psf_font) 
    : fb(framebuffer), width(framebuffer->width), height(framebuffer->height), 
      pitch(framebuffer->pitch), bpp(framebuffer->bpp) {
    
    font_header = (PSF1_Header*)psf_font;
    
    // The glyphs start immediately after the header
    glyph_buffer = (const uint8_t*)psf_font + sizeof(PSF1_Header);
}

void Renderer::putPixel(int x, int y, std::uint32_t color) {
    if (x < 0 || y < 0 || x >= (int)width || y >= (int)height) return;
    std::uint64_t offset = y * pitch + (x * 4);
    *(volatile std::uint32_t*)((std::uint8_t*)fb->address + offset) = color;
}

void Renderer::drawRect(int x, int y, int w, int h, std::uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)width) w = (int)width - x;
    if (y + h > (int)height) h = (int)height - y;
    if (w <= 0 || h <= 0) return;

    // Use SSE to fill rects
    for (int i = 0; i < h; i++) {
        std::uint8_t* row_start = (std::uint8_t*)fb->address + ((y + i) * pitch) + (x * 4);
        int pixels_remaining = w;
        int p_off = 0;

        // 1. Align to 16 bytes (optional, but good practice)
        while (pixels_remaining > 0 && ((uint64_t)(row_start + p_off * 4) & 0xF) != 0) {
            *(uint32_t*)(row_start + p_off * 4) = color;
            p_off++;
            pixels_remaining--;
        }

        // 2. Fast SSE Block Write (4 pixels at a time)
        while (pixels_remaining >= 4) {
            _sse_store_128(row_start + p_off * 4, color);
            p_off += 4;
            pixels_remaining -= 4;
        }

        // 3. Cleanup leftovers
        while (pixels_remaining > 0) {
            *(uint32_t*)(row_start + p_off * 4) = color;
            p_off++;
            pixels_remaining--;
        }
    }
}

void Renderer::drawLine(int x0, int y0, int x1, int y1, std::uint32_t color) {
    int dx = iabs(x1 - x0);
    int dy = -iabs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        putPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Renderer::drawFilledCircle(int cx, int cy, int radius, std::uint32_t color) {
    int r2 = radius * radius;
    
    for (int y = -radius; y <= radius; y++) {
        int target_y = cy + y;
        if (target_y < 0 || target_y >= (int)height) continue;

        int span_width = (int)sqrt((float)(r2 - y*y));
        int start_x = cx - span_width;
        int end_x = cx + span_width;

        if (start_x < 0) start_x = 0;
        if (end_x >= (int)this->width) end_x = (int)this->width - 1;
        if (start_x > end_x) continue;

        // USE SSE HERE TOO
        std::uint8_t* row_start = (std::uint8_t*)fb->address + (target_y * pitch) + (start_x * 4);
        int pixels = end_x - start_x + 1;
        int p_off = 0;

        while (pixels >= 4) {
            _sse_storeu_128(row_start + p_off * 4, color);
            p_off += 4;
            pixels -= 4;
        }
        while (pixels > 0) {
            *(uint32_t*)(row_start + p_off * 4) = color;
            p_off++;
            pixels--;
        }
    }
}

void Renderer::clear(std::uint32_t color) {
    bool contiguous = (pitch == width * 4);
    
    if (contiguous) {
        std::size_t total_pixels = width * height;
        std::uint8_t* ptr = (std::uint8_t*)fb->address;
        
        while (total_pixels >= 4) {
            _sse_storeu_128(ptr, color);
            ptr += 16; 
            total_pixels -= 4;
        }
        while (total_pixels > 0) {
            *(uint32_t*)ptr = color;
            ptr += 4;
            total_pixels--;
        }
    } else {
        for (std::uint32_t y = 0; y < height; y++) {
            std::uint8_t* row = (std::uint8_t*)fb->address + (y * pitch);
            int pixels = width;
            while (pixels >= 4) {
                _sse_storeu_128(row, color);
                row += 16;
                pixels -= 4;
            }
            while (pixels > 0) {
                *(uint32_t*)row = color;
                row += 4;
                pixels--;
            }
        }
    }
}

void Renderer::drawChar(int x, int y, char c, std::uint32_t color, int scale) {
    unsigned char uc = (unsigned char)c;
    const uint8_t* glyph = glyph_buffer + (uc * font_header->charsize);

    for (int row = 0; row < font_header->charsize; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if ((bits >> (7 - col)) & 1) {
                if (scale == 1) {
                    putPixel(x + col, y + row, color);
                } else {
                    drawRect(x + (col * scale), y + (row * scale), scale, scale, color);
                }
            }
        }
    }
}

void Renderer::drawString(int x, int y, const char* str, std::uint32_t color, int scale) {
    int cursor_x = x;
    int cursor_y = y;
    int font_w = 8 * scale;
    int font_h = font_header->charsize * scale;

    while (*str) {
        if (*str == '\n') {
            cursor_x = x;
            cursor_y += font_h;
        } else {
            drawChar(cursor_x, cursor_y, *str, color, scale);
            cursor_x += font_w;
        }
        str++;
    }
}

// 24-bit RGB Source
void Renderer::renderBitmap(int x_start, int y_start, int w, int h, const std::uint8_t* data) {
    std::uint8_t* fb_addr = static_cast<std::uint8_t*>(fb->address);
    for (int y = 0; y < h; y++) {
        int target_y = y_start + y;
        if (target_y < 0 || target_y >= (int)height) continue;
        volatile std::uint32_t* row_ptr = (volatile std::uint32_t*)(fb_addr + (target_y * pitch));
        for (int x = 0; x < w; x++) {
            int target_x = x_start + x;
            if (target_x < 0 || target_x >= (int)width) continue;
            std::size_t index = (y * w + x) * 3;
            std::uint32_t color = (data[index] << 16) | (data[index + 1] << 8) | data[index + 2];
            row_ptr[target_x] = color;
        }
    }
}

// 32-bit ARGB Source (Direct Blit)
void Renderer::renderBitmap32(int x_start, int y_start, int w, int h, const std::uint32_t* data) {
    std::uint8_t* fb_addr = static_cast<std::uint8_t*>(fb->address);
    for (int y = 0; y < h; y++) {
        int target_y = y_start + y;
        if (target_y < 0 || target_y >= (int)height) continue;
        
        volatile std::uint32_t* row_ptr = (volatile std::uint32_t*)(fb_addr + (target_y * pitch));
        
        // Use a simpler copy loop or memcpy for speed
        for (int x = 0; x < w; x++) {
            int target_x = x_start + x;
            if (target_x < 0 || target_x >= (int)width) continue;
            
            // Source is simply 32-bit to 32-bit copy
            row_ptr[target_x] = data[y * w + x];
        }
    }
}

void Renderer::renderBitmapColored(int x_start, int y_start, int w, int h, const std::uint8_t* data, std::uint32_t color) {
    std::uint8_t* fb_addr = static_cast<std::uint8_t*>(fb->address);
    for (int y = 0; y < h; y++) {
        int target_y = y_start + y;
        if (target_y < 0 || target_y >= (int)height) continue;
        volatile std::uint32_t* row_ptr = (volatile std::uint32_t*)(fb_addr + (target_y * pitch));
        for (int x = 0; x < w; x++) {
            int target_x = x_start + x;
            if (target_x < 0 || target_x >= (int)width) continue;
            std::size_t index = (y * w + x) * 3;
            if (data[index] | data[index + 1] | data[index + 2]) {
                row_ptr[target_x] = color;
            } 
        }
    }
}   