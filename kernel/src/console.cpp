#include "console.h"
#include <limine.h>
#include <cstddef> 

extern "C" void *memcpy(void *dest, const void *src, std::size_t n);
extern "C" void *memset(void *s, int c, std::size_t n);

Console::Console(Renderer* r) : renderer(r), cursor_x(0), cursor_y(0), fgColor(0xFFFFFF), bgColor(0x000000), scale(1) {
    // Dynamic Font Calculation
    font_w = renderer->getFontWidth();
    font_h = renderer->getFontHeight();

    width_chars = renderer->getWidth() / (font_w * scale);
    height_chars = renderer->getHeight() / (font_h * scale);
}

void Console::setColor(std::uint32_t fg, std::uint32_t bg) {
    fgColor = fg;
    bgColor = bg;
}

void Console::drawCursor(bool state) {
    std::uint32_t color = state ? fgColor : bgColor;
    renderer->drawRect(
        cursor_x * font_w * scale, 
        cursor_y * font_h * scale, 
        font_w * scale, 
        font_h * scale, 
        color
    );
}

void Console::putString(int x_char, int y_char, const char* str, uint32_t fg, uint32_t bg) {
    int x_px = x_char * font_w * scale;
    int y_px = y_char * font_h * scale;
    int len = 0;
    while(str[len]) len++;

    renderer->drawRect(x_px, y_px, len * font_w * scale, font_h * scale, bg);
    renderer->drawString(x_px, y_px, str, fg, scale);
}

void Console::putChar(char c) {
    if (c == '\n') {
        newLine();
        return;
    }

    if (c == '\b') { 
        if (cursor_x > 0) {
            cursor_x--;
            // Erase with background color
            renderer->drawRect(cursor_x * font_w * scale, 
                               cursor_y * font_h * scale, 
                               font_w * scale, 
                               font_h * scale, 
                               bgColor);
        }
        return;
    }

    renderer->drawChar(cursor_x * font_w * scale, 
                       cursor_y * font_h * scale, 
                       c, fgColor, scale);

    cursor_x++;
    if (cursor_x >= width_chars) {
        newLine();
    }
}

void Console::print(const char* str) {
    while (*str) {
        putChar(*str++);
    }
}

void Console::newLine() {
    cursor_x = 0;
    cursor_y++;

    if (cursor_y >= height_chars) {
        scroll();
        cursor_y = height_chars - 1;
    }
}

void Console::scroll() {
    std::uint32_t* fb_addr = (std::uint32_t*)renderer->getFramebuffer()->address;
    std::uint64_t pitch = renderer->getFramebuffer()->pitch; 
    std::uint64_t total_height = renderer->getHeight();

    std::uint8_t* dest = (std::uint8_t*)fb_addr;
    
    // Scroll start Y is 1 row height down
    uint64_t scroll_start_y = font_h * scale;
    
    dest += scroll_start_y * pitch;

    std::uint64_t row_byte_size = (font_h * scale) * pitch;
    std::uint8_t* src = dest + row_byte_size;

    std::size_t bytes_to_move = (total_height * pitch) - (scroll_start_y * pitch) - row_byte_size;

    memcpy(dest, src, bytes_to_move);

    // Clear the last line
    std::uint64_t bottom_y = (height_chars - 1) * font_h * scale;
    renderer->drawRect(0, bottom_y, renderer->getWidth(), font_h * scale, bgColor);
}