#include "text_editor.h"
#include "../globals.h"
#include "../input.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../timer.h"
#include "../io.h"

// --- Configuration ---
#define EDITOR_MAX_BUF 65536
#define TAB_SIZE 4

// --- VS Code Dark Theme Colors ---
#define BG_COLOR      0x1E1E1E // Dark Grey Background
#define LINE_NUM_COL  0x858585 // Grey Line Numbers
#define CURSOR_COL    0xAEAFAD // Light Grey Cursor
#define STATUS_BG     0x007ACC // Blue Status Bar
#define STATUS_FG     0xFFFFFF

// Syntax Colors
#define COL_DEFAULT   0xD4D4D4 // Light Grey
#define COL_KEYWORD   0xC586C0 // Purple (Control flow: if, while, func)
#define COL_TYPE      0x569CD6 // Blue (int, str, void)
#define COL_FUNC      0xDCDCAA // Light Yellow (printf, malloc, drawrect)
#define COL_STRING    0xCE9178 // Orange/Brown
#define COL_NUMBER    0xB5CEA8 // Light Green
#define COL_COMMENT   0x6A9955 // Green
#define COL_PREPROC   0xE06C75 // Red/Pink (.header)
#define COL_OPERATOR  0xD4D4D4 // White

// --- Editor Global State ---
static bool g_editor_running = true;
static char* g_buffer = nullptr;
static uint32_t g_buf_len = 0;
static uint32_t g_cursor_pos = 0;
static char g_filename[32];
static bool g_dirty = false;

// Viewport state
static int g_scroll_line = 0;
static int g_screen_rows = 0;
static int g_screen_cols = 0;

// --- Syntax Helpers ---

static bool is_separator(char c) {
    return (c == ' ' || c == '\n' || c == '\t' || c == '\r' || 
            c == '(' || c == ')' || c == '{' || c == '}' || 
            c == '[' || c == ']' || c == ',' || c == ';' || 
            c == '+' || c == '-' || c == '*' || c == '/' || 
            c == '=' || c == '<' || c == '>' || c == '!');
}

static uint32_t get_keyword_color(const char* word, int len) {
    // 1. Control Flow & Structure (Purple)
    if ((len==4 && memcmp(word, "func", 4)==0) ||
        (len==4 && memcmp(word, "done", 4)==0) ||
        (len==2 && memcmp(word, "if", 2)==0)   ||
        (len==4 && memcmp(word, "else", 4)==0) ||
        (len==5 && memcmp(word, "while", 5)==0)||
        (len==3 && memcmp(word, "asm", 3)==0)  || 
        (len==6 && memcmp(word, "return", 6)==0)) {
        return COL_KEYWORD;
    }

    // 2. Types (Blue)
    if ((len==3 && memcmp(word, "int", 3)==0) ||
        (len==4 && memcmp(word, "int8", 4)==0) ||
        (len==3 && memcmp(word, "flt", 3)==0) ||
        (len==4 && memcmp(word, "flt4", 4)==0)||
        (len==4 && memcmp(word, "char", 4)==0)||
        (len==3 && memcmp(word, "str", 3)==0) ||
        (len==3 && memcmp(word, "ptr", 3)==0) ||
        (len==4 && memcmp(word, "void", 4)==0)) {
        return COL_TYPE;
    }

    // 3. System Intrinsics / Functions (Yellow)
    if ((len==6 && memcmp(word, "printf", 6)==0)   ||
        (len==8 && memcmp(word, "printbuf", 8)==0) ||
        (len==6 && memcmp(word, "malloc", 6)==0)   ||
        (len==7 && memcmp(word, "rmalloc", 7)==0)  ||
        (len==5 && memcmp(word, "sleep", 5)==0)    ||
        (len==8 && memcmp(word, "drawrect", 8)==0) ||
        (len==9 && memcmp(word, "disk_read", 9)==0)||
        (len==10&& memcmp(word, "disk_write", 10)==0)||
        (len==5 && memcmp(word, "getch", 5)==0)    ||
        (len==3 && memcmp(word, "inb", 3)==0)      ||
        (len==3 && memcmp(word, "inw", 3)==0)      ||
        (len==3 && memcmp(word, "inl", 3)==0)      ||
        (len==4 && memcmp(word, "outb", 4)==0)     ||
        (len==4 && memcmp(word, "outw", 4)==0)     ||
        (len==4 && memcmp(word, "outl", 4)==0)     ||
        (len==3 && memcmp(word, "mov", 3)==0)      || // ASM mnemonics
        (len==3 && memcmp(word, "add", 3)==0)      ||
        (len==3 && memcmp(word, "sub", 3)==0)      ||
        (len==3 && memcmp(word, "int", 3)==0)) {
        return COL_FUNC;
    }

    // Default variable/identifier color
    return COL_DEFAULT; 
}

// --- IO Helpers ---

static void save_file() {
    if (Fat32::getInstance().write_file(g_filename, g_buffer, g_buf_len)) {
        g_dirty = false;
        // Visual feedback
        int status_y = g_screen_rows * 16;
        g_renderer->drawRect(0, status_y, g_renderer->getWidth(), 16, 0x00FF00); // Flash Green
        g_renderer->drawString(0, status_y, " FILE SAVED SUCCESSFULLY ", 0x000000);
        sleep_ms(500);
    } else {
        int status_y = g_screen_rows * 16;
        g_renderer->drawRect(0, status_y, g_renderer->getWidth(), 16, 0xFF0000); // Flash Red
        g_renderer->drawString(0, status_y, " ERROR SAVING FILE ", 0xFFFFFF);
        sleep_ms(500);
    }
}

// --- Input Processing ---

static void process_key(char c) {
    if (c == 27) { // ESC
        g_editor_running = false;
        return;
    }

    // Ctrl+S to Save
    if (g_ctrl_pressed && (c == 's' || c == 'S' || c == 19)) {
        save_file();
        return;
    }

    // Navigation (Using Extended ASCII keys 128+)
    if (c == (char)KEY_UP) {
        int curr = g_cursor_pos;
        int col = 0;
        // Find start of current line
        while (curr > 0 && g_buffer[curr-1] != '\n') { curr--; col++; }
        
        if (curr > 0) {
            curr--; // Step back to previous line
            // Find start of previous line
            while (curr > 0 && g_buffer[curr-1] != '\n') curr--;
            
            // Calculate length of previous line
            int line_len = 0;
            int temp = curr;
            while (temp < (int)g_buf_len && g_buffer[temp] != '\n') { temp++; line_len++; }
            
            if (col > line_len) col = line_len;
            g_cursor_pos = curr + col;
        }
        return;
    }

    if (c == (char)KEY_DOWN) {
        int curr = g_cursor_pos;
        // Find end of current line
        while (curr < (int)g_buf_len && g_buffer[curr] != '\n') curr++;
        
        if (curr < (int)g_buf_len) {
            // We are at the newline char
            int col = 0;
            int temp = g_cursor_pos;
            // Calculate current column
            while (temp > 0 && g_buffer[temp-1] != '\n') { temp--; col++; }

            curr++; // Move to start of next line
            int next_start = curr;
            int line_len = 0;
            while (curr < (int)g_buf_len && g_buffer[curr] != '\n') { curr++; line_len++; }
            
            if (col > line_len) col = line_len;
            g_cursor_pos = next_start + col;
        }
        return;
    }

    if (c == (char)KEY_LEFT) {
        if (g_cursor_pos > 0) g_cursor_pos--;
        return;
    }
    
    if (c == (char)KEY_RIGHT) {
        if (g_cursor_pos < g_buf_len) g_cursor_pos++;
        return;
    }

    if (c == '\b') { // Backspace
        if (g_cursor_pos > 0) {
            memmove(g_buffer + g_cursor_pos - 1, g_buffer + g_cursor_pos, g_buf_len - g_cursor_pos);
            g_buf_len--;
            g_cursor_pos--;
            g_dirty = true;
        }
        return;
    }

    // Normal Typing (including Tab and Enter)
    if (c >= 32 || c == '\n' || c == '\t') {
        if (g_buf_len < EDITOR_MAX_BUF - 1) {
            memmove(g_buffer + g_cursor_pos + 1, g_buffer + g_cursor_pos, g_buf_len - g_cursor_pos);
            g_buffer[g_cursor_pos] = c;
            g_cursor_pos++;
            g_buf_len++;
            g_dirty = true;
        }
    }
}

// --- Rendering ---

static void render_editor() {
    // 1. Calculate Cursor Position & Scrolling
    int current_line = 0;
    for (uint32_t i = 0; i < g_cursor_pos; i++) {
        if (g_buffer[i] == '\n') current_line++;
    }
    
    // Auto-scroll
    if (current_line < g_scroll_line) g_scroll_line = current_line;
    if (current_line >= g_scroll_line + g_screen_rows) g_scroll_line = current_line - g_screen_rows + 1;

    // 2. Clear Screen
    g_renderer->clear(BG_COLOR);

    int draw_y = 0;
    int draw_x = 0;
    uint32_t i = 0;
    
    // Skip lines that are scrolled off-screen
    int skip = g_scroll_line;
    while (skip > 0 && i < g_buf_len) {
        if (g_buffer[i] == '\n') skip--;
        i++;
    }

    // 3. Render Loop
    bool in_string = false;
    bool in_comment = false;
    bool in_preproc = false;

    // Draw Line Number Gutter Background
    g_renderer->drawRect(0, 0, 32, g_renderer->getHeight(), 0x252526);

    // Initial Line Number
    char lnbuf[8];
    sprintf(lnbuf, "%3d", g_scroll_line + 1);
    g_renderer->drawString(0, 0, lnbuf, LINE_NUM_COL);
    draw_x = 40; // Start text after gutter

    while (i < g_buf_len && draw_y < g_screen_rows) {
        char c = g_buffer[i];
        
        // --- State Management ---
        // Reset line-based states on newline
        if (c == '\n') {
            in_comment = false;
            in_preproc = false;
            in_string = false; // Strings don't span lines in CPL usually
            
            draw_y++;
            draw_x = 0;
            
            // Draw next line number
            if (draw_y < g_screen_rows) {
                sprintf(lnbuf, "%3d", g_scroll_line + draw_y + 1);
                g_renderer->drawString(0, draw_y * 16, lnbuf, LINE_NUM_COL);
            }
            draw_x = 40;
            i++;
            continue;
        }

        // Check for Comment Start
        if (!in_string && !in_comment && c == '/' && i+1 < g_buf_len && g_buffer[i+1] == '/') {
            in_comment = true;
        }

        // Check for Preprocessor
        if (!in_string && !in_comment && !in_preproc && c == '.') {
            in_preproc = true;
        }

        // Check for String Toggle
        if (!in_comment && c == '"') {
            in_string = !in_string;
            g_renderer->drawChar(draw_x, draw_y * 16, c, COL_STRING);
            draw_x += 8;
            i++;
            continue;
        }

        // --- Color Decision ---
        uint32_t color = COL_DEFAULT;

        if (in_comment) {
            color = COL_COMMENT;
        } else if (in_string) {
            color = COL_STRING;
        } else if (in_preproc) {
            color = COL_PREPROC;
        } else {
            // Advanced Token Highlighting
            if (c >= '0' && c <= '9') {
                color = COL_NUMBER;
            } 
            else if (is_separator(c)) {
                color = COL_OPERATOR;
            }
            else {
                // It's likely a word. Peek ahead to check if it's a keyword.
                // Only scan if this is the start of a word
                if (i == 0 || is_separator(g_buffer[i-1])) {
                    int len = 0;
                    while (i + len < g_buf_len && !is_separator(g_buffer[i + len])) {
                        len++;
                    }
                    
                    uint32_t kw_col = get_keyword_color(&g_buffer[i], len);
                    
                    // If it is a keyword/type, draw the whole word now to ensure uniform color
                    if (kw_col != COL_DEFAULT) {
                        for(int k=0; k<len; k++) {
                            g_renderer->drawChar(draw_x, draw_y * 16, g_buffer[i+k], kw_col);
                            draw_x += 8;
                        }
                        i += len;
                        continue; // Skip the main loop increment since we handled the word
                    }
                }
            }
        }

        // Handle Tab
        if (c == '\t') {
            draw_x += (8 * TAB_SIZE);
        } else {
            g_renderer->drawChar(draw_x, draw_y * 16, c, color);
            draw_x += 8;
        }
        
        i++;
    }

    // 4. Draw Cursor (Blinking logic could be added, but solid for now)
    int col_on_line = 0;
    int temp = g_cursor_pos;
    while (temp > 0 && g_buffer[temp-1] != '\n') { temp--; col_on_line++; }
    
    // Calculate tab compensation for cursor
    int visual_col = 0;
    temp = g_cursor_pos - col_on_line; // Start of line
    for(int k=0; k<col_on_line; k++) {
        if (g_buffer[temp + k] == '\t') visual_col += TAB_SIZE;
        else visual_col++;
    }

    int rel_y = current_line - g_scroll_line;
    if (rel_y >= 0 && rel_y < g_screen_rows) {
        int cursor_screen_x = 40 + (visual_col * 8);
        g_renderer->drawRect(cursor_screen_x, rel_y * 16, 2, 16, CURSOR_COL); 
    }

    // 5. Draw Status Bar
    int status_y = g_screen_rows * 16;
    g_renderer->drawRect(0, status_y, g_renderer->getWidth(), 16, STATUS_BG);
    
    char status[128];
    sprintf(status, "  %s %s  |  CPL  |  Ln %d, Col %d  |  UTF-8", 
        g_filename, g_dirty ? "*" : "", current_line + 1, col_on_line + 1);
    
    g_renderer->drawString(0, status_y, status, STATUS_FG);
}

// --- Main Entry Point ---

void run_text_editor(const char* filename) {
    g_editor_running = true;
    g_dirty = false;
    g_scroll_line = 0;
    strcpy(g_filename, filename);

    // Calculate layout based on font size (8x16)
    g_screen_cols = g_renderer->getWidth() / 8;
    g_screen_rows = (g_renderer->getHeight() / 16) - 1; // Leave room for status bar

    g_buffer = (char*)malloc(EDITOR_MAX_BUF);
    if (!g_buffer) {
        printf("Editor: OOM\n");
        return;
    }
    memset(g_buffer, 0, EDITOR_MAX_BUF);
    g_buf_len = 0;
    g_cursor_pos = 0;

    // Load File
    if (Fat32::getInstance().read_file(filename, g_buffer, EDITOR_MAX_BUF)) {
        g_buf_len = strlen(g_buffer);
        // Ensure cursor is at end or 0 (0 for now)
        g_cursor_pos = 0;
    } 
    
    sleep_ms(200); // Debounce enter key from shell
    render_editor(); 

    while (g_editor_running) {
        char c = input_check_char();
        if (c != 0) {
            process_key(c);
            render_editor();
        } else {
            // Poll hooks (mouse, network) to keep OS alive
            check_input_hooks();
            asm volatile("hlt");
        }
    }

    free(g_buffer);
    if (g_renderer) g_renderer->clear(0x000000);
}