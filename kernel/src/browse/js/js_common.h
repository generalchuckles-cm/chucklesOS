#ifndef JS_COMMON_H
#define JS_COMMON_H

#include "../browse.h" 

// Context passed to every JS handler
struct JSContext {
    BrowserApp* app;
    bool exit_flag; // If true, the engine stops executing the current script
};

// Helper to parse a string argument: alert("HELLO") -> extracts HELLO
// Returns cursor advance amount
static int js_parse_string_arg(const char* cursor, char* out_buf, int max_len) {
    int i = 0;
    
    // Skip whitespace, opening parenthesis, AND equals signs (for assignments)
    while(cursor[i] && (cursor[i] == ' ' || cursor[i] == '(' || cursor[i] == '=')) i++;
    
    // Check for quote
    if (cursor[i] != '"' && cursor[i] != '\'') return 0;
    char quote_type = cursor[i];
    i++; // Skip opening quote

    int out_i = 0;
    // Copy until closing quote
    while(cursor[i] && cursor[i] != quote_type && out_i < max_len - 1) {
        out_buf[out_i++] = cursor[i++];
    }
    out_buf[out_i] = 0;
    
    // Skip closing quote
    if (cursor[i] == quote_type) i++;
    
    // Skip trailing parens/semicolons/spaces
    while(cursor[i] && (cursor[i] == ' ' || cursor[i] == ')' || cursor[i] == ';')) i++;
    
    return i; 
}

#endif