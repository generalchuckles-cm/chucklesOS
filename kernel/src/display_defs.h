#ifndef DISPLAY_DEFS_H
#define DISPLAY_DEFS_H

#include <cstdint>

enum DisplayMode {
    MODE_32BIT,
    MODE_24BIT,
    MODE_16BIT,
    MODE_8BIT,
    MODE_GRAYSCALE,
    MODE_4COLOR,
    MODE_MONOCHROME
};

struct VideoModeInfo {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

#define MAX_HARDWARE_MODES 16

struct HardwareDisplayInfo {
    uint32_t mode_count;
    VideoModeInfo modes[MAX_HARDWARE_MODES];
    uint32_t native_width;
    uint32_t native_height;
};

struct DisplaySettings {
    DisplayMode mode;
    int target_width;
    int target_height;
    bool dirty;
};

struct Theme {
    uint32_t win_bg;         // Window content surface
    uint32_t border;         // Subtle 1px outline
    uint32_t title_active;   // Active title bar
    uint32_t title_inactive; // Dimmed inactive title bar
    uint32_t accent;         // Highlight / Accent elements
    uint32_t text_color;     // Primary typography
    uint32_t text_dim;       // Secondary typography
    uint32_t btn_close;      // macOS-style red circular close
    uint32_t desktop_bg;     // Deep charcoal desktop canvas
    uint32_t taskbar_bg;     // Floating dock / bottom bar
};

extern DisplaySettings g_display_settings;
extern HardwareDisplayInfo g_hardware_display;
extern Theme g_theme;

#endif
