#include "browse.h"
#include "req.h"
#include "error_pages.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../net/network.h"
#include "../input.h"
#include "../fs/fat32.h"
#include "js/engine.h"

BrowserApp::BrowserApp() : page_content(nullptr), content_len(0), scroll_y(0), scripts_executed(false) {
    page_content = (char*)malloc(262144); // 256 KB
}

BrowserApp::~BrowserApp() {
    if (page_content) free(page_content);
}

void BrowserApp::on_init(Window* win) {
    my_window = win;
    if (page_content) {
        strcpy(page_content, "<h1>Welcome to ChucklesBrowse!</h1><p>Use the 'browse' command in the terminal to navigate.</p>");
        content_len = strlen(page_content);
        scripts_executed = true; // Don't run scripts on the welcome page
    }
    on_draw();
}

void BrowserApp::on_input(char c) {
    if (c == (char)KEY_UP) {
        scroll_y -= 20;
        if (scroll_y < 0) scroll_y = 0;
    }
    if (c == (char)KEY_DOWN) {
        scroll_y += 20;
    }
    on_draw();
}

void BrowserApp::on_draw() {
    if (!my_window) return;
    my_window->renderer->clear(0xFFFFFF); // White background
    parse_and_render_html();
}

void BrowserApp::parse_url(const char* url, char* host, int max_host, char* path, int max_path) {
    const char* p = url;
    if (memcmp(p, "http://", 7) == 0) p += 7;
    
    int i = 0;
    while (*p && *p != '/' && i < max_host - 1) {
        host[i++] = *p++;
    }
    host[i] = 0;
    
    if (*p == '/') {
        i = 0;
        while (*p && i < max_path - 1) {
            path[i++] = *p++;
        }
        path[i] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }
}

int BrowserApp::get_status_code(const char* headers) {
    const char* p = headers;
    while (*p && *p != ' ') p++;
    if (!*p) return 0;
    
    p++;
    int code = 0;
    while (*p >= '0' && *p <= '9') {
        code = code * 10 + (*p - '0');
        p++;
    }
    return code;
}

bool BrowserApp::find_header_value(const char* headers, const char* key, char* out_val, int max_len) {
    const char* p = strstr(headers, key);
    if (!p) return false;
    
    p += strlen(key);
    while (*p == ':' || *p == ' ') p++;
    
    int i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < max_len - 1) {
        out_val[i++] = *p++;
    }
    out_val[i] = 0;
    return true;
}

void BrowserApp::navigate(const char* url, int redirect_count) {
    if (!page_content) return;
    
    if (redirect_count > 5) {
        strcpy(page_content, "<h1>Error</h1><p>Too many redirects.</p>");
        content_len = strlen(page_content);
        scripts_executed = true; // No scripts on error page
        on_draw();
        return;
    }

    // --- LOCAL FILE CHECK ---
    if (Fat32::getInstance().read_file(url, page_content, 262143)) {
        printf("BROWSE: Loaded local file %s\n", url);
        
        content_len = 0;
        while(content_len < 262143 && page_content[content_len] != 0) content_len++;
        
        sprintf(my_window->title, "Local: %s", url);
        scroll_y = 0;
        scripts_executed = false; // RESET FLAG: Allow scripts to run once for this new page
        on_draw();
        return; 
    }
    // ------------------------
    
    memset(page_content, 0, 262144);
    content_len = 0;
    scroll_y = 0;
    scripts_executed = false; // RESET FLAG: Allow scripts for network page
    on_draw();
    
    char host[128];
    char path[256];
    parse_url(url, host, 128, path, 256);
    
    sprintf(my_window->title, "Loading: %s", host);
    
    uint32_t ip = NetworkStack::getInstance().dns_lookup(host);
    
    if (ip == 0) {
        strcpy(page_content, "<h1>DNS Error</h1><p>Could not resolve host.</p>");
        scripts_executed = true;
    } else {
        TcpSocket sock;
        if (sock.connect(ip, 80)) {
            char req[512];
            sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
            sock.send((uint8_t*)req, strlen(req));
            
            content_len = sock.recv((uint8_t*)page_content, 262143);
            sock.close();
            
            int status = get_status_code(page_content);
            if (status == HTTP_301_MOVED_PERMANENTLY || status == HTTP_302_FOUND) {
                char new_url[256];
                if (find_header_value(page_content, "Location", new_url, 256)) {
                    sprintf(my_window->title, "Redirecting...");
                    navigate(new_url, redirect_count + 1);
                    return; 
                }
            }
        } else {
            strcpy(page_content, "<h1>Connection Failed</h1><p>Could not connect to host.</p>");
            scripts_executed = true;
        }
    }
    
    if (strlen(page_content) > 0) content_len = strlen(page_content);
    strcpy(my_window->title, host);
    on_draw();
}

int BrowserApp::strcicmp(const char* s1, const char* s2) {
    while (*s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) return 1; 
        s1++;
        s2++;
    }
    return 0; 
}

void BrowserApp::parse_and_render_html() {
    if (!page_content) return;

    enum State { TEXT, TAG, SCRIPT, STYLE };
    State state = TEXT;

    int x = 5, y = 5 - scroll_y;
    int font_w = 8, font_h = 16;
    int scale = 1;
    uint32_t color = 0x000000;

    char tag_buf[32];
    int tag_idx = 0;
    
    char script_buf[4096];
    int script_idx = 0;

    const char* body = strstr(page_content, "\r\n\r\n");
    if (!body) body = page_content;
    else body += 4;

    for (const char* p = body; *p && p < page_content + content_len; p++) {
        char c = *p;

        switch (state) {
            case TEXT:
                if (c == '<') {
                    state = TAG;
                    tag_idx = 0;
                    memset(tag_buf, 0, 32);
                } else {
                    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
                    
                    if (c == '&' && memcmp(p, "&lt;", 4) == 0) { c = '<'; p += 3; }
                    else if (c == '&' && memcmp(p, "&gt;", 4) == 0) { c = '>'; p += 3; }
                    else if (c == '&' && memcmp(p, "&amp;", 5) == 0) { c = '&'; p += 4; }
                    else if (c == '&' && memcmp(p, "&nbsp;", 6) == 0) { c = ' '; p += 5; }

                    my_window->renderer->drawChar(x, y, c, color, scale);
                    x += font_w * scale;
                    if (x > my_window->width - (font_w * scale) - 5) {
                        x = 5;
                        y += font_h * scale;
                    }
                }
                break;
            
            case TAG:
                if (c == '>') {
                    state = TEXT;
                    if (strcicmp(tag_buf, "script") == 0) {
                        state = SCRIPT;
                        script_idx = 0;
                        memset(script_buf, 0, 4096);
                    }
                    if (strcicmp(tag_buf, "style") == 0) state = STYLE;
                    
                    if (strcicmp(tag_buf, "p") == 0 || strcicmp(tag_buf, "div") == 0 || strcicmp(tag_buf, "br") == 0) {
                        x = 5; y += font_h * scale;
                    }
                    if (strcicmp(tag_buf, "h1") == 0) scale = 2;
                    if (strcicmp(tag_buf, "/h1") == 0) scale = 1;
                    if (strcicmp(tag_buf, "a") == 0) color = 0x0000FF;
                    if (strcicmp(tag_buf, "/a") == 0) color = 0x000000;
                } else if (c != ' ' && c != '/' && tag_idx < 31) {
                    tag_buf[tag_idx++] = c;
                }
                break;

            case SCRIPT:
                if (c == '<' && (p+8 < page_content + content_len) && strcicmp(p, "</script>") == 0) {
                    p += 8; 
                    state = TEXT;
                    
                    if (!scripts_executed) {
                        scripts_executed = true; 
                        // If JSEngine returns true, the page content has been replaced/redirected.
                        // We must stop rendering immediately to avoid reading freed/changed memory.
                        if (JSEngine::run(this, script_buf)) {
                            return; 
                        }
                    }
                } 
                else {
                    if (script_idx < 4095) {
                        script_buf[script_idx++] = c;
                        script_buf[script_idx] = 0;
                    }
                }
                break;
            
            case STYLE:
                if (c == '<' && strcicmp(p, "</style>") == 0) {
                    p += 7; 
                    state = TEXT;
                }
                break;
        }
    }
}