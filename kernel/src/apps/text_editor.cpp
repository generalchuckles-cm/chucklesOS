#include "text_editor.h"
#include "../globals.h"
#include "../input.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../timer.h"

// Colors (VS Code Dark)
#define BG_COLOR      0x1E1E1E 
#define LINE_NUM_COL  0x858585 
#define CURSOR_COL    0xAEAFAD 
#define STATUS_BG     0x007ACC 
#define STATUS_FG     0xFFFFFF

// Syntax
#define COL_DEFAULT   0xD4D4D4 
#define COL_KEYWORD   0xC586C0 
#define COL_TYPE      0x569CD6 
#define COL_FUNC      0xDCDCAA 
#define COL_STRING    0xCE9178 
#define COL_NUMBER    0xB5CEA8 
#define COL_COMMENT   0x6A9955 
#define COL_PREPROC   0xE06C75 

TextEditorApp::TextEditorApp(const char* file) {
    if (file) strcpy(filename, file);
    else strcpy(filename, "untitled.c");
    
    max_buf = 65536;
    buffer = (char*)malloc(max_buf);
    memset(buffer, 0, max_buf);
    buf_len = 0;
    cursor_pos = 0;
    scroll_line = 0;
    dirty = false;
}

TextEditorApp::~TextEditorApp() {
    if (buffer) {
        free(buffer);
        buffer = nullptr; // Safety: Prevent use-after-free
    }
}

void TextEditorApp::on_init(Window* win) {
    my_window = win;
    
    if (Fat32::getInstance().read_file(filename, buffer, max_buf)) {
        buf_len = strlen(buffer);
        cursor_pos = 0;
    }
    
    // Clear once
    my_window->renderer->clear(BG_COLOR);
}

void TextEditorApp::save_file() {
    if (Fat32::getInstance().write_file(filename, buffer, buf_len)) {
        dirty = false;
        // Status flash handled in render
    }
}

void TextEditorApp::on_input(char c) {
    if (c == 19) { // Ctrl+S
        save_file();
        return;
    }
    
    // Navigation
    if (c == (char)KEY_UP) {
        // Simple logic: move back to previous newline
        int curr = cursor_pos;
        int col = 0;
        while (curr > 0 && buffer[curr-1] != '\n') { curr--; col++; }
        if (curr > 0) {
            curr--; 
            while (curr > 0 && buffer[curr-1] != '\n') curr--;
            int line_len = 0;
            int temp = curr;
            while (temp < (int)buf_len && buffer[temp] != '\n') { temp++; line_len++; }
            if (col > line_len) col = line_len;
            cursor_pos = curr + col;
        }
        return;
    }
    if (c == (char)KEY_DOWN) {
        int curr = cursor_pos;
        while (curr < (int)buf_len && buffer[curr] != '\n') curr++;
        if (curr < (int)buf_len) {
            int col = 0;
            int temp = cursor_pos;
            while (temp > 0 && buffer[temp-1] != '\n') { temp--; col++; }
            curr++; 
            int next_start = curr;
            int line_len = 0;
            while (curr < (int)buf_len && buffer[curr] != '\n') { curr++; line_len++; }
            if (col > line_len) col = line_len;
            cursor_pos = next_start + col;
        }
        return;
    }
    if (c == (char)KEY_LEFT) { if(cursor_pos > 0) cursor_pos--; return; }
    if (c == (char)KEY_RIGHT) { if(cursor_pos < buf_len) cursor_pos++; return; }

    if (c == '\b') {
        if (cursor_pos > 0) {
            memmove(buffer + cursor_pos - 1, buffer + cursor_pos, buf_len - cursor_pos);
            buf_len--;
            cursor_pos--;
            dirty = true;
        }
        return;
    }

    if (c >= 32 || c == '\n' || c == '\t') {
        if (buf_len < max_buf - 1) {
            memmove(buffer + cursor_pos + 1, buffer + cursor_pos, buf_len - cursor_pos);
            buffer[cursor_pos] = c;
            cursor_pos++;
            buf_len++;
            dirty = true;
        }
    }
}

bool TextEditorApp::is_separator(char c) {
    return (c == ' ' || c == '\n' || c == '\t' || c == '\r' || 
            c == '(' || c == ')' || c == '{' || c == '}' || 
            c == '[' || c == ']' || c == ',' || c == ';' || 
            c == '+' || c == '-' || c == '*' || c == '/' || 
            c == '=' || c == '<' || c == '>' || c == '!');
}

uint32_t TextEditorApp::get_keyword_color(const char* word, int len) {
    // C Keywords
    if ((len==2 && memcmp(word, "if", 2)==0)   ||
        (len==4 && memcmp(word, "else", 4)==0) ||
        (len==5 && memcmp(word, "while", 5)==0)||
        (len==3 && memcmp(word, "for", 3)==0)  || 
        (len==6 && memcmp(word, "return", 6)==0)) return COL_KEYWORD;

    // Types
    if ((len==3 && memcmp(word, "int", 3)==0) ||
        (len==4 && memcmp(word, "void", 4)==0)||
        (len==4 && memcmp(word, "char", 4)==0)||
        (len==5 && memcmp(word, "float", 5)==0)) return COL_TYPE;

    // Functions
    if ((len==6 && memcmp(word, "printf", 6)==0) ||
        (len==6 && memcmp(word, "malloc", 6)==0) ||
        (len==5 && memcmp(word, "sleep", 5)==0)  ||
        (len==4 && memcmp(word, "puts", 4)==0)   ||
        (len==4 && memcmp(word, "exit", 4)==0)   ||
        (len==5 && memcmp(word, "getch", 5)==0)  ||
        (len==7 && memcmp(word, "putchar", 7)==0)) return COL_FUNC;

    return COL_DEFAULT; 
}

void TextEditorApp::on_draw() {
    Renderer* r = my_window->renderer;
    r->clear(BG_COLOR);
    
    int w = my_window->width;
    int h = my_window->height;
    int cols = w / 8;
    int rows = (h / 16) - 1;

    // Calc Cursor Line
    int current_line = 0;
    for(uint32_t i=0; i<cursor_pos; i++) if(buffer[i] == '\n') current_line++;
    
    if (current_line < scroll_line) scroll_line = current_line;
    if (current_line >= scroll_line + rows) scroll_line = current_line - rows + 1;

    // Gutter
    r->drawRect(0, 0, 32, h - 16, 0x252526);

    int draw_y = 0;
    int draw_x = 40;
    uint32_t i = 0;
    
    // Skip
    int skip = scroll_line;
    while(skip > 0 && i < buf_len) { if(buffer[i] == '\n') skip--; i++; }

    char lnbuf[8];
    sprintf(lnbuf, "%3d", scroll_line + 1);
    r->drawString(0, 0, lnbuf, LINE_NUM_COL);

    bool in_string = false;
    bool in_preproc = false;

    while (i < buf_len && draw_y < rows) {
        char c = buffer[i];
        
        if (c == '\n') {
            draw_y++;
            draw_x = 40;
            in_string = false; 
            in_preproc = false;
            
            if (draw_y < rows) {
                sprintf(lnbuf, "%3d", scroll_line + draw_y + 1);
                r->drawString(0, draw_y * 16, lnbuf, LINE_NUM_COL);
            }
            i++;
            continue;
        }

        // #include detection
        if (!in_string && !in_preproc && c == '#') in_preproc = true;
        if (!in_string && in_preproc) {
            r->drawChar(draw_x, draw_y * 16, c, COL_PREPROC);
            draw_x += 8;
            i++;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            r->drawChar(draw_x, draw_y * 16, c, COL_STRING);
            draw_x += 8;
            i++;
            continue;
        }

        uint32_t col = COL_DEFAULT;
        if (in_string) col = COL_STRING;
        else if (c >= '0' && c <= '9') col = COL_NUMBER;
        else if (is_separator(c)) col = COL_DEFAULT;
        else {
            // Keyword check
            if (i == 0 || is_separator(buffer[i-1])) {
                int len = 0;
                while (i + len < buf_len && !is_separator(buffer[i+len])) len++;
                uint32_t kw = get_keyword_color(&buffer[i], len);
                if (kw != COL_DEFAULT) {
                    for(int k=0; k<len; k++) {
                        r->drawChar(draw_x, draw_y * 16, buffer[i+k], kw);
                        draw_x += 8;
                    }
                    i += len;
                    continue;
                }
            }
        }

        if (c == '\t') draw_x += 32;
        else {
            r->drawChar(draw_x, draw_y * 16, c, col);
            draw_x += 8;
        }
        i++;
    }

    // Cursor
    int col_on_line = 0;
    int temp = cursor_pos;
    while(temp > 0 && buffer[temp-1] != '\n') { temp--; col_on_line++; }
    
    int visual_col = 0;
    temp = cursor_pos - col_on_line;
    for(int k=0; k<col_on_line; k++) {
        if(buffer[temp+k] == '\t') visual_col += 4; // Approx
        else visual_col++;
    }

    int rel_y = current_line - scroll_line;
    if (rel_y >= 0 && rel_y < rows) {
        r->drawRect(40 + visual_col*8, rel_y*16, 2, 16, CURSOR_COL);
    }

    // Status Bar
    int status_y = h - 16;
    r->drawRect(0, status_y, w, 16, STATUS_BG);
    char status[128];
    sprintf(status, " %s %s  |  Ln %d, Col %d", filename, dirty?"*":"", current_line+1, visual_col+1);
    r->drawString(0, status_y, status, STATUS_FG);
}