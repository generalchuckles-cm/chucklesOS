#ifndef CONSOLE_H
#define CONSOLE_H

#include "render.h"

class Console {
public:
    Console(Renderer* renderer);

    void putChar(char c);
    void print(const char* str);
    
    // Set colors
    void setColor(std::uint32_t fg, std::uint32_t bg);

    // Draw (or erase) the cursor block at the current position
    // state: true = draw block, false = erase block (draw background)
    void drawCursor(bool state);

    // New: Draw a string at a specific character row/column, bypassing the cursor
    // Used for status bars, etc.
    void putString(int x_char, int y_char, const char* str, uint32_t fg, uint32_t bg);

private:
    void scroll();
    void newLine();

    Renderer* renderer;
    int font_w;
    int font_h;
    int cursor_x;
    int cursor_y;
    int width_chars;
    int height_chars;
    
    std::uint32_t fgColor;
    std::uint32_t bgColor;
    int scale;
};

#endif