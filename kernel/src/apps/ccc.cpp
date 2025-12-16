#include "ccc.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../cppstd/stdlib.h"

// Increased buffer size to prevent overflows
#define MAX_SRC_SIZE 131072 
#define MAX_BIN_SIZE 65536

struct CodeBuffer {
    uint8_t* buf;
    int idx;
    uint8_t* data_section;
    int data_idx;
};

void c_emit8(CodeBuffer* cb, uint8_t val) { cb->buf[cb->idx++] = val; }
void c_emit32(CodeBuffer* cb, uint32_t val) { *(uint32_t*)&cb->buf[cb->idx] = val; cb->idx += 4; }
void c_emit64(CodeBuffer* cb, uint64_t val) { *(uint64_t*)&cb->buf[cb->idx] = val; cb->idx += 8; }

bool c_match(const char* src, int& pos, const char* keyword) {
    int len = strlen(keyword);
    if (memcmp(src + pos, keyword, len) == 0) {
        pos += len;
        return true;
    }
    return false;
}

void c_skip_white(const char* src, int& pos) {
    while (src[pos] == ' ' || src[pos] == '\n' || src[pos] == '\t' || src[pos] == '\r') pos++;
}

int c_parse_int(const char* src, int& pos) {
    int val = 0;
    while(src[pos] >= '0' && src[pos] <= '9') {
        val = val * 10 + (src[pos] - '0');
        pos++;
    }
    return val;
}

// --- PREPROCESSOR ---
char* preprocess_source(const char* filename, int depth) {
    if (depth > 10) return nullptr;

    char* raw_src = (char*)malloc(MAX_SRC_SIZE);
    if (!Fat32::getInstance().read_file(filename, raw_src, MAX_SRC_SIZE)) {
        free(raw_src);
        return nullptr;
    }

    // Output buffer
    char* out_buf = (char*)malloc(MAX_SRC_SIZE);
    memset(out_buf, 0, MAX_SRC_SIZE);
    int out_idx = 0;
    int pos = 0;

    while (raw_src[pos] != 0 && out_idx < MAX_SRC_SIZE - 1) {
        // Handle #include
        if (raw_src[pos] == '#') {
            int peek = pos + 1;
            if (memcmp(raw_src + peek, "include", 7) == 0) {
                pos += 8;
                c_skip_white(raw_src, pos);
                
                char quote = raw_src[pos];
                if (quote == '"' || quote == '<') {
                    pos++;
                    char inc_file[64];
                    int f_i = 0;
                    char end_quote = (quote == '<') ? '>' : '"';
                    while (raw_src[pos] != 0 && raw_src[pos] != end_quote && raw_src[pos] != '\n') {
                        inc_file[f_i++] = raw_src[pos++];
                    }
                    inc_file[f_i] = 0;
                    if (raw_src[pos] == end_quote) pos++;

                    // Recursively load
                    char* sub_content = preprocess_source(inc_file, depth + 1);
                    if (sub_content) {
                        int len = strlen(sub_content);
                        if (out_idx + len < MAX_SRC_SIZE) {
                            memcpy(out_buf + out_idx, sub_content, len);
                            out_idx += len;
                        } else {
                            printf("CCC: Preprocessor buffer overflow!\n");
                        }
                        free(sub_content);
                    } else {
                        // FIX: If system header (<...>) missing, ignore it.
                        // This allows #include <stdio.h> to pass without error.
                        if (quote == '<') {
                            // printf("CCC: Ignoring system header <%s>\n", inc_file);
                        } else {
                            printf("CCC: Error - Include file '%s' not found.\n", inc_file);
                            free(raw_src); free(out_buf);
                            return nullptr;
                        }
                    }
                    continue;
                }
            }
        }

        out_buf[out_idx++] = raw_src[pos++];
    }

    free(raw_src);
    return out_buf;
}

void ccc_compile(const char* in_file, const char* out_file) {
    printf("CCC: Compiling %s...\n", in_file);

    char* src = preprocess_source(in_file, 0);
    if (!src) {
        printf("CCC: Preprocessing failed.\n");
        return;
    }

    CodeBuffer cb;
    cb.buf = (uint8_t*)malloc(MAX_BIN_SIZE);
    cb.idx = 0;
    cb.data_section = (uint8_t*)malloc(MAX_BIN_SIZE);
    cb.data_idx = 0;

    memset(cb.buf, 0, MAX_BIN_SIZE);
    memset(cb.data_section, 0, MAX_BIN_SIZE);

    // Prologue
    c_emit8(&cb, 0x55); // push rbp
    c_emit8(&cb, 0x48); c_emit8(&cb, 0x89); c_emit8(&cb, 0xE5); // mov rbp, rsp

    int pos = 0;
    while (src[pos] != 0) {
        c_skip_white(src, pos);
        
        // Skip remaining directives (like #define which we don't support yet)
        if (src[pos] == '#') {
            while (src[pos] != '\n' && src[pos] != 0) pos++;
            continue;
        }

        // Handle "int main() {"
        if (c_match(src, pos, "int") || c_match(src, pos, "void")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "main")) {
                c_skip_white(src, pos);
                if (c_match(src, pos, "()")) {
                    c_skip_white(src, pos);
                    if (c_match(src, pos, "{")) continue;
                }
            }
        }

        // Functions
        if (c_match(src, pos, "printf")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "(")) {
                c_skip_white(src, pos);
                if (src[pos] == '"') {
                    pos++; 
                    char str_buf[256];
                    int s_i = 0;
                    while (src[pos] != '"' && src[pos] != 0) str_buf[s_i++] = src[pos++];
                    str_buf[s_i] = 0;
                    pos++; 
                    
                    int str_offset = cb.data_idx;
                    strcpy((char*)&cb.data_section[cb.data_idx], str_buf);
                    cb.data_idx += s_i + 1;

                    c_emit8(&cb, 0x48); c_emit8(&cb, 0xC7); c_emit8(&cb, 0xC7); 
                    c_emit32(&cb, str_offset);
                    
                    c_emit8(&cb, 0xB8); c_emit32(&cb, 1); // SYS_PRINT
                    c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
                }
            }
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (c_match(src, pos, "puts")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "(")) {
                c_skip_white(src, pos);
                if (src[pos] == '"') {
                    pos++; 
                    char str_buf[256];
                    int s_i = 0;
                    while (src[pos] != '"' && src[pos] != 0) str_buf[s_i++] = src[pos++];
                    str_buf[s_i++] = '\n'; 
                    str_buf[s_i] = 0;
                    pos++; 
                    
                    int str_offset = cb.data_idx;
                    strcpy((char*)&cb.data_section[cb.data_idx], str_buf);
                    cb.data_idx += s_i + 1;

                    c_emit8(&cb, 0x48); c_emit8(&cb, 0xC7); c_emit8(&cb, 0xC7); 
                    c_emit32(&cb, str_offset);
                    
                    c_emit8(&cb, 0xB8); c_emit32(&cb, 1); 
                    c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
                }
            }
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (c_match(src, pos, "sleep")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "(")) {
                c_skip_white(src, pos);
                int ms = c_parse_int(src, pos);
                
                c_emit8(&cb, 0x48); c_emit8(&cb, 0xC7); c_emit8(&cb, 0xC7);
                c_emit32(&cb, ms);
                
                c_emit8(&cb, 0xB8); c_emit32(&cb, 4); // SYS_SLEEP
                c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
            }
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (c_match(src, pos, "exit")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "(")) {
                c_skip_white(src, pos);
                int code = c_parse_int(src, pos);
                // SYS_EXIT = 0
                c_emit8(&cb, 0xB8); c_emit32(&cb, 0);
                c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
            }
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (c_match(src, pos, "getch")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "(")) {
                c_skip_white(src, pos);
                if (c_match(src, pos, ")")) {
                    c_emit8(&cb, 0xB8); c_emit32(&cb, 2); // SYS_GETCH
                    c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
                }
            }
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (c_match(src, pos, "putchar")) {
            c_skip_white(src, pos);
            if (c_match(src, pos, "(")) {
                c_skip_white(src, pos);
                if (src[pos] == '\'') {
                    pos++;
                    char c = src[pos++];
                    if (src[pos] == '\'') pos++;
                    
                    char str_buf[2] = {c, 0};
                    int str_offset = cb.data_idx;
                    strcpy((char*)&cb.data_section[cb.data_idx], str_buf);
                    cb.data_idx += 2;

                    c_emit8(&cb, 0x48); c_emit8(&cb, 0xC7); c_emit8(&cb, 0xC7); 
                    c_emit32(&cb, str_offset);
                    
                    c_emit8(&cb, 0xB8); c_emit32(&cb, 1); 
                    c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
                }
            }
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (c_match(src, pos, "return")) {
            c_skip_white(src, pos);
            int ret_val = c_parse_int(src, pos);
            c_emit8(&cb, 0xB8); c_emit32(&cb, 0); 
            c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
            while(src[pos] != ';' && src[pos] != 0) pos++;
            pos++;
        }
        else if (src[pos] == '}') {
            c_emit8(&cb, 0xB8); c_emit32(&cb, 0); 
            c_emit8(&cb, 0xCD); c_emit8(&cb, 0x80);
            pos++;
        }
        else {
            pos++;
        }
    }

    uint32_t code_len = cb.idx;
    uint32_t base_addr = 0x00400000;
    
    // Patch pointers
    for (int i = 0; i < code_len - 6; i++) {
        if (cb.buf[i] == 0x48 && cb.buf[i+1] == 0xC7 && cb.buf[i+2] == 0xC7) {
            uint32_t data_offset = *(uint32_t*)&cb.buf[i+3];
            uint32_t abs_addr = base_addr + code_len + data_offset;
            *(uint32_t*)&cb.buf[i+3] = abs_addr;
        }
    }
    
    uint8_t* final_bin = (uint8_t*)malloc(code_len + cb.data_idx);
    memcpy(final_bin, cb.buf, code_len);
    memcpy(final_bin + code_len, cb.data_section, cb.data_idx);
    
    if (Fat32::getInstance().write_file(out_file, final_bin, code_len + cb.data_idx)) {
        printf("CCC: Compilation Successful! Output: %s (%d bytes)\n", out_file, code_len + cb.data_idx);
    } else {
        printf("CCC: Write Failed.\n");
    }

    free(src);
    free(cb.buf);
    free(cb.data_section);
    free(final_bin);
}