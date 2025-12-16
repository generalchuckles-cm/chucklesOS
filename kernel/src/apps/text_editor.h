#ifndef TEXT_EDITOR_H
#define TEXT_EDITOR_H

#include "../gui/window.h"

class TextEditorApp : public WindowApp {
public:
    TextEditorApp(const char* file);
    ~TextEditorApp();

    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;

private:
    Window* my_window;
    char filename[32];
    
    char* buffer;
    uint32_t buf_len;
    uint32_t max_buf;
    
    uint32_t cursor_pos;
    int scroll_line;
    bool dirty;
    
    // Helpers
    void save_file();
    void process_key(char c);
    void render_text();
    bool is_separator(char c);
    uint32_t get_keyword_color(const char* word, int len);
};

#endif