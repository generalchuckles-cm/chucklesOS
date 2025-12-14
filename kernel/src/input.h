#ifndef INPUT_H
#define INPUT_H

#include <cstdint>

// Special Key Definitions (Mapped to Extended ASCII range 128+)
#define KEY_UP     ((char)0x80)
#define KEY_DOWN   ((char)0x81)
#define KEY_LEFT   ((char)0x82)
#define KEY_RIGHT  ((char)0x83)
#define KEY_PGUP   ((char)0x84)
#define KEY_PGDN   ((char)0x85)
#define KEY_HOME   ((char)0x86)
#define KEY_END    ((char)0x87)

#define KEY_CTRL   0x1D
#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36

// Global State
extern bool g_shift_pressed;
extern bool g_ctrl_pressed;
extern bool g_using_interrupts;

// Key State Map
extern volatile bool g_key_state[128];

// --- Mouse State ---
extern volatile int g_mouse_x;
extern volatile int g_mouse_y;
extern volatile bool g_mouse_left;
extern volatile bool g_mouse_right;
extern volatile bool g_mouse_middle;

// --- Core Input API ---
void input_buffer_push(char c);
char input_get_char();
char input_check_char();

// --- Hardware Hooks ---
void input_register_poller(void (*callback)());
void check_input_hooks();
void input_process_scancode(uint8_t scancode);

#endif