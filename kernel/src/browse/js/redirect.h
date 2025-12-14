#ifndef JS_REDIRECT_H
#define JS_REDIRECT_H

#include "js_common.h"
#include "../../cppstd/stdio.h"

// Handles: window.location = "..." 
static void js_handle_redirect(JSContext* ctx, const char* args_cursor) {
    char url[256];
    int advance = js_parse_string_arg(args_cursor, url, 256);
    
    if (advance > 0 && ctx->app) {
        printf("JS REDIRECT: Navigating to %s\n", url);
        
        // 1. Perform Navigation
        ctx->app->navigate(url);
        
        // 2. Stop the script engine immediately
        // The page context has changed; continuing execution would be undefined behavior.
        ctx->exit_flag = true;
    }
}

#endif