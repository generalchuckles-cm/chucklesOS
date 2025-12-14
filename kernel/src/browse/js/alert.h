#ifndef JS_ALERT_H
#define JS_ALERT_H

#include "js_common.h"
#include "../../globals.h"
#include "../../gui/window.h"
#include "../../input.h"
#include "../../cppstd/stdio.h"
#include "../../drv/input/elan_touch.h" // Required to keep the mouse moving

static void js_handle_alert(JSContext* ctx, const char* args_cursor) {
    char msg[128];
    int advance = js_parse_string_arg(args_cursor, msg, 128);
    
    if (advance > 0 && ctx->app && ctx->app->my_window) {
        printf("JS ALERT: %s\n", msg);
        
        Renderer* r = ctx->app->my_window->renderer;
        int w = ctx->app->my_window->width;
        int h = ctx->app->my_window->height;
        
        // 1. Draw Alert Box directly to the Window's backing buffer
        // Center a red box
        int box_w = 400;
        int box_h = 100;
        int box_x = (w - box_w) / 2;
        int box_y = (h - box_h) / 2;
        
        if (box_x < 0) box_x = 0;
        
        // Red border
        r->drawRect(box_x, box_y, box_w, box_h, 0xFF0000); 
        // White interior
        r->drawRect(box_x + 2, box_y + 2, box_w - 4, box_h - 4, 0xFFFFFF); 
        
        // 2. Draw Text
        r->drawString(box_x + 10, box_y + 10, "JAVASCRIPT ALERT", 0x000000, 2);
        r->drawString(box_x + 10, box_y + 40, msg, 0x0000FF, 1);
        r->drawString(box_x + 10, box_y + 70, "Press ENTER to continue...", 0x888888, 1);
        
        // 3. Modal Loop (Non-Blocking)
        // We must manually pump the OS loop here so the mouse moves and 
        // the WindowManager can handle the focus click.
        while(true) {
            // A. Poll Hardware
            check_input_hooks();
            ElanTouchpad::getInstance().poll();
            
            // B. Update Window Manager (Handles Mouse movement and clicking to focus)
            WindowManager::getInstance().update();
            
            // C. Render the screen (Composites our Alert window onto the screen)
            WindowManager::getInstance().render(g_renderer);
            
            // D. Check for Key Press (Non-blocking)
            char c = input_check_char(); 
            
            // Only accept input if our window is actually focused!
            // This prevents typing into other windows from closing the alert accidentally,
            // though for a simple OS, allowing any Enter press is often acceptable.
            if (ctx->app->my_window->is_focused) {
                if (c == '\n' || c == 27) break; // Enter or Esc to close
            } else {
                // If we aren't focused, we might want to discard the char or let it buffer,
                // but input_check_char consumes it. For now, consume and ignore.
            }
            
            // E. Yield CPU to prevent burning 100% usage
            asm volatile("hlt");
        }
        
        // 4. Trigger redraw of the page to clear the alert box visuals
        ctx->app->on_draw();
    }
}

#endif