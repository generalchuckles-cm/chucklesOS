#ifndef JS_ENGINE_H
#define JS_ENGINE_H

#include "js_common.h"
#include "alert.h"
#include "redirect.h"
#include "../../cppstd/string.h"

class JSEngine {
public:
    // Returns true if the page was redirected/unloaded
    static bool run(BrowserApp* app, const char* script) {
        if (!script || !app) return false;
        
        JSContext ctx;
        ctx.app = app;
        ctx.exit_flag = false;
        
        const char* p = script;
        
        // Simple Lexer Loop
        while (*p) {
            // Check exit flag
            if (ctx.exit_flag) return true;

            // Skip whitespace
            while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
            if (!*p) break;
            
            // Check for 'alert' function call
            if (memcmp(p, "alert", 5) == 0) {
                js_handle_alert(&ctx, p + 5);
                
                // Skip past this statement
                // FIX: Stop at semicolon OR newline to handle missing semicolons
                while(*p && *p != ';' && *p != '\n') p++;
                if(*p) p++;
            }
            // Check for 'window.location' assignment
            else if (memcmp(p, "window.location", 15) == 0) {
                // Look for '='
                const char* assign = p + 15;
                while (*assign == ' ' || *assign == '=') assign++;
                
                // Pass the pointer exactly at the quote (or just before it)
                js_handle_redirect(&ctx, assign); 
                
                if (ctx.exit_flag) return true; // Stop immediately after redirect

                while(*p && *p != ';' && *p != '\n') p++;
                if(*p) p++;
            }
            else {
                // Unknown token, skip to next delimiter
                while(*p && *p != ';' && *p != '\n') p++;
                if(*p) p++;
            }
        }
        
        return ctx.exit_flag;
    }
};

#endif