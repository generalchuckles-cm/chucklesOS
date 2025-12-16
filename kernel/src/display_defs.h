#ifndef DISPLAY_DEFS_H
#define DISPLAY_DEFS_H

#include <cstdint>

enum DisplayMode {
    MODE_32BIT,       // Standard ARGB
    MODE_24BIT,       // RGB (Simulated)
    MODE_16BIT,       // RGB 565
    MODE_8BIT,        // RGB 332
    MODE_GRAYSCALE,   // True Color Monochrome
    MODE_4COLOR,      // CGA Palette 1 (Black, Cyan, Magenta, White)
    MODE_MONOCHROME   // 1-Bit B&W
};

struct DisplaySettings {
    DisplayMode mode;
    int target_width;
    int target_height;
    bool dirty; // Flag to tell WindowManager to re-allocate buffer
};

struct Theme {
    uint32_t win_bg;        // Main face color
    uint32_t border_light;  // Highlight (Top/Left)
    uint32_t border_dark;   // Shadow (Bottom/Right)
    uint32_t title_active_1; // Gradient Start
    uint32_t title_active_2; // Gradient End
    uint32_t title_inactive; // Inactive Title
    uint32_t text_color;    // Window Text
    uint32_t btn_face;      // Button color
    bool flat;              // If true, disable 3D bevels
};

extern DisplaySettings g_display_settings;
extern Theme g_theme;

#endif