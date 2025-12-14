#include "input.h"
#include "io.h"

#define K_INPUT_BUF_SIZE 256
static volatile char g_key_buffer[K_INPUT_BUF_SIZE];
static volatile int g_head = 0;
static volatile int g_tail = 0;

static void (*g_external_poller)() = nullptr;

bool g_shift_pressed = false;
bool g_ctrl_pressed = false;
bool g_using_interrupts = false;

volatile bool g_key_state[128] = {0};

volatile int g_mouse_x = 0;
volatile int g_mouse_y = 0;
volatile bool g_mouse_left = false;
volatile bool g_mouse_right = false;
volatile bool g_mouse_middle = false;

// --- PS/2 Maps (US Layout Set 1) ---

static const char kbd_US_low [128] = {
    // 0x00 - 0x09
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8',
    // 0x0A - 0x13
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
    // 0x14 - 0x1D
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0 /*CTRL*/,
    // 0x1E - 0x27
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    // 0x28 - 0x31
    '\'', '`', 0 /*LShift*/, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    // 0x32 - 0x3B
    'm', ',', '.', '/', 0 /*RShift*/, '*', 0 /*Alt*/, ' ', 0 /*Caps*/, 0 /*F1*/,
    // 0x3C - 0x45
    0 /*F2*/, 0 /*F3*/, 0 /*F4*/, 0 /*F5*/, 0 /*F6*/, 0 /*F7*/, 0 /*F8*/, 0 /*F9*/, 0 /*F10*/, 0 /*Num*/,
    // 0x46 - 0x4F
    0 /*Scroll*/, KEY_HOME, KEY_UP, KEY_PGUP, '-', KEY_LEFT, '5', KEY_RIGHT, '+', KEY_END,
    // 0x50 - 0x59
    KEY_DOWN, KEY_PGDN, 0 /*Ins*/, 0 /*Del*/, 0 /*SysReq*/, 0 /*?*/, 0 /*?*/, 0 /*F11*/, 0 /*F12*/, 0,
    // 0x5A... (Padding to 128)
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char kbd_US_high [128] = {
    // 0x00 - 0x09
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*',
    // 0x0A - 0x13
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',
    // 0x14 - 0x1D
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    // 0x1E - 0x27
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    // 0x28 - 0x31
    '\"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    // 0x32 - 0x3B
    'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0,
    // 0x3C - 0x45 (F-Keys etc ignore shift)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x46 - 0x4F (Keypad)
    0, KEY_HOME, KEY_UP, KEY_PGUP, '-', KEY_LEFT, '5', KEY_RIGHT, '+', KEY_END,
    // 0x50 - 0x59
    KEY_DOWN, KEY_PGDN, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x5A...
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static char scancode_to_ascii(std::uint8_t code) {
    if (code > 127) return 0; // Prevent Out of Bounds
    return g_shift_pressed ? kbd_US_high[code] : kbd_US_low[code];
}

void input_buffer_push(char c) {
    if (c == 0) return;
    int next = (g_head + 1) % K_INPUT_BUF_SIZE;
    if (next != g_tail) {
        g_key_buffer[g_head] = c;
        g_head = next;
    }
}

char input_check_char() {
    if (g_head == g_tail) return 0;
    char c = g_key_buffer[g_tail];
    g_tail = (g_tail + 1) % K_INPUT_BUF_SIZE;
    return c;
}

char input_get_char() {
    while (true) {
        check_input_hooks();
        if (g_using_interrupts) cli();
        
        if (g_head != g_tail) {
            char c = g_key_buffer[g_tail];
            g_tail = (g_tail + 1) % K_INPUT_BUF_SIZE;
            if (g_using_interrupts) sti();
            return c;
        }
        
        if (g_using_interrupts) {
            asm volatile("sti; hlt");
        } else {
            asm volatile("pause");
        }
    }
}

void input_register_poller(void (*callback)()) {
    g_external_poller = callback;
}

void input_process_scancode(uint8_t scancode) {
    // 0xE0 is the Extended Scan Code escape (sent by Arrow Keys before the actual code)
    // We ignore it, because the next interrupt will contain the actual code (e.g. 0x48 for Up)
    // which our table handles correctly.
    if (scancode == 0xE0) return;

    // 1. Update Raw State
    if (scancode & 0x80) {
        // Key Release
        g_key_state[scancode & 0x7F] = false;
    } else {
        // Key Press
        g_key_state[scancode] = true;
    }

    // 2. Handle Modifiers
    if (scancode == 0x2A || scancode == 0x36) { g_shift_pressed = true; return; }
    if (scancode == 0xAA || scancode == 0xB6) { g_shift_pressed = false; return; }
    if (scancode == 0x1D) { g_ctrl_pressed = true; return; }
    if (scancode == 0x9D) { g_ctrl_pressed = false; return; }

    // 3. Buffer ASCII (only on press)
    if (!(scancode & 0x80)) {
        char ascii = scancode_to_ascii(scancode);
        if (ascii != 0) {
            input_buffer_push(ascii);
        }
    }
}

void check_input_hooks() {
    if (g_external_poller) g_external_poller();

    if (!g_using_interrupts) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            input_process_scancode(sc);
        }
    }
}