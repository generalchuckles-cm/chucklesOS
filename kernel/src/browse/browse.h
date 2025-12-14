#ifndef BROWSE_H
#define BROWSE_H

#include "../gui/window.h"
#include "../net/tcp.h"

class BrowserApp : public WindowApp {
public:
    BrowserApp();
    ~BrowserApp();

    void on_init(Window* win) override;
    void on_draw() override;
    void on_input(char c) override;

    // Main function to load a URL, now with redirect tracking
    void navigate(const char* url, int redirect_count = 0);

    // Public for JS Access
    Window* my_window;

private:
    // Buffer to hold the downloaded page content
    char* page_content;
    int content_len;
    
    int scroll_y;
    
    // NEW: Prevents infinite alert loops
    bool scripts_executed;

    // Helper functions
    void parse_and_render_html();
    void parse_url(const char* url, char* host, int max_host, char* path, int max_path);
    int get_status_code(const char* headers);
    // Finds a header value like "Location" in an HTTP response
    bool find_header_value(const char* headers, const char* key, char* out_val, int max_len);
    // Case-insensitive compare for HTML tags
    int strcicmp(const char* s1, const char* s2);
};

#endif