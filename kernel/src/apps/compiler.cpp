#include "compiler.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../cppstd/stdlib.h"

#define MAX_SRC 131072
#define MAX_SYMBOLS 256
#define LOAD_BASE 0x400000

struct Symbol {
    char name[32];
    int stack_offset; 
};

struct CompilerState {
    char* src;
    char* src_end;
    char* current;
    
    uint8_t* text_section;
    uint32_t text_idx;
    
    uint8_t* data_section;
    uint32_t data_idx;

    Symbol symbols[MAX_SYMBOLS];
    int symbol_count;
    int local_stack_size;
};

// --- Emitter ---
void emit8(CompilerState* s, uint8_t val) { s->text_section[s->text_idx++] = val; }
void emit32(CompilerState* s, uint32_t val) { 
    *(uint32_t*)&s->text_section[s->text_idx] = val; s->text_idx += 4; 
}
void emit64(CompilerState* s, uint64_t val) {
    *(uint64_t*)&s->text_section[s->text_idx] = val; s->text_idx += 8;
}

// --- Lexer ---
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

void skip_whitespace(CompilerState* s) {
    while (s->current < s->src_end) {
        if (memcmp(s->current, "//", 2) == 0) {
            while (*s->current != '\n' && *s->current != 0) s->current++;
        } else if (is_space(*s->current)) {
            s->current++;
        } else {
            break;
        }
    }
}

bool match(CompilerState* s, const char* what) {
    skip_whitespace(s);
    int len = strlen(what);
    if (memcmp(s->current, what, len) == 0) {
        if (is_alnum(what[len-1]) && is_alnum(s->current[len])) return false;
        s->current += len;
        return true;
    }
    return false;
}

void parse_token(CompilerState* s, char* out) {
    skip_whitespace(s);
    int i = 0;
    while (is_alnum(*s->current) || *s->current == ':' || *s->current == '_') {
        if (i < 63) out[i++] = *s->current;
        s->current++;
    }
    out[i] = 0;
}

int parse_number(CompilerState* s) {
    skip_whitespace(s);
    int num = 0;
    while (is_digit(*s->current)) {
        num = num * 10 + (*s->current - '0');
        s->current++;
    }
    return num;
}

int find_symbol(CompilerState* s, const char* name) {
    for(int i=0; i<s->symbol_count; i++) {
        if(strcmp(s->symbols[i].name, name) == 0) return s->symbols[i].stack_offset;
    }
    return 0; 
}

bool process_includes(char* buffer, int max_len) {
    char* read = buffer; 
    char* write = buffer; 
    while(*read) {
        if (memcmp(read, "#include", 8) == 0) {
            while(*read != '\n' && *read != 0) read++;
        } else {
            *write++ = *read++;
        }
    }
    *write = 0;
    return true;
}

void compile_expression(CompilerState* s);

void compile_primary(CompilerState* s) {
    skip_whitespace(s);
    
    if (is_digit(*s->current)) {
        int val = parse_number(s);
        emit8(s, 0x48); emit8(s, 0xC7); emit8(s, 0xC0); emit32(s, val);
        emit8(s, 0x50);
    } 
    else if (*s->current == '(') {
        if (memcmp(s->current, "(char)", 6) == 0) {
            s->current += 6;
            compile_primary(s);
            emit8(s, 0x58);
            emit8(s, 0x48); emit8(s, 0x83); emit8(s, 0xE0); emit8(s, 0xFF);
            emit8(s, 0x50);
        } else {
            s->current++;
            compile_expression(s);
            match(s, ")");
        }
    }
    else if (match(s, "std::atoi")) {
        match(s, "(");
        compile_expression(s); 
        match(s, ")");
        
        emit8(s, 0x5F); // POP RDI
        
        // Inline ATOI implementation
        emit8(s, 0x48); emit8(s, 0x31); emit8(s, 0xC0); // xor rax, rax
        emit8(s, 0x48); emit8(s, 0x31); emit8(s, 0xC9); // xor rcx, rcx
        
        // Loop Start:
        emit8(s, 0x8A); emit8(s, 0x0F); // mov cl, [rdi]
        emit8(s, 0x84); emit8(s, 0xC9); // test cl, cl
        // jz +15 bytes
        emit8(s, 0x74); emit8(s, 0x0F);
        
        emit8(s, 0x80); emit8(s, 0xE9); emit8(s, 0x30); // sub cl, '0'
        emit8(s, 0x48); emit8(s, 0x6B); emit8(s, 0xC0); emit8(s, 0x0A); // imul rax, 10
        emit8(s, 0x48); emit8(s, 0x01); emit8(s, 0xC8); // add rax, rcx
        emit8(s, 0x48); emit8(s, 0xFF); emit8(s, 0xC7); // inc rdi
        emit8(s, 0xEB); emit8(s, 0xEB); // jmp -21
        
        emit8(s, 0x50); // push result
    }
    else {
        char name[64];
        parse_token(s, name);
        skip_whitespace(s);
        if (*s->current == '[') {
            s->current++;
            compile_expression(s);
            match(s, "]");
            emit8(s, 0x5B);
            int offset = find_symbol(s, name);
            emit8(s, 0x48); emit8(s, 0x8B); emit8(s, 0x45); emit8(s, (uint8_t)(-offset));
            emit8(s, 0x48); emit8(s, 0x8D); emit8(s, 0x04); emit8(s, 0xD8);
            emit8(s, 0x48); emit8(s, 0x8B); emit8(s, 0x00);
            emit8(s, 0x50);
        } else {
            int offset = find_symbol(s, name);
            emit8(s, 0x48); emit8(s, 0x8B); emit8(s, 0x45); emit8(s, (uint8_t)(-offset));
            emit8(s, 0x50);
        }
    }
    
    if (memcmp(s->current, "++", 2) == 0) {
        s->current += 2;
        emit8(s, 0x58); emit8(s, 0x48); emit8(s, 0xFF); emit8(s, 0xC0); emit8(s, 0x50); 
    }
}

void compile_relational(CompilerState* s) {
    compile_primary(s);
    skip_whitespace(s);
    if (*s->current == '<' || *s->current == '>') {
        char op = *s->current;
        s->current++;
        compile_primary(s);
        emit8(s, 0x5B); emit8(s, 0x58);
        emit8(s, 0x48); emit8(s, 0x39); emit8(s, 0xD8);
        emit8(s, 0x0F); 
        if (op == '<') emit8(s, 0x9C); 
        else emit8(s, 0x9F); 
        emit8(s, 0x48); emit8(s, 0x0F); emit8(s, 0xB6); emit8(s, 0xC0);
        emit8(s, 0x50);
    }
}

void compile_expression(CompilerState* s) {
    compile_relational(s);
}

void compile_cout_chain(CompilerState* s) {
    while (match(s, "<<")) {
        if (match(s, "std::endl") || match(s, "endl")) {
            s->data_section[s->data_idx] = '\n'; 
            s->data_section[s->data_idx+1] = 0;
            uint64_t addr = LOAD_BASE + 0x2000 + s->data_idx;
            s->data_idx += 2;
            emit8(s, 0x48); emit8(s, 0xBF); emit64(s, addr); 
            emit8(s, 0xB8); emit32(s, 1); emit8(s, 0xCD); emit8(s, 0x80);
        } else {
            compile_expression(s);
            emit8(s, 0x5E); // Pop to RSI
            uint32_t fmt_off = s->data_idx;
            s->data_section[s->data_idx++] = '%'; s->data_section[s->data_idx++] = 'c'; s->data_section[s->data_idx++] = 0;
            emit8(s, 0x48); emit8(s, 0xBF); emit64(s, LOAD_BASE + 0x2000 + fmt_off);
            emit8(s, 0xB8); emit32(s, 1); emit8(s, 0xCD); emit8(s, 0x80);
        }
    }
    match(s, ";");
}

void compile_statement(CompilerState* s) {
    if (match(s, "if")) {
        match(s, "(");
        compile_expression(s);
        match(s, ")");
        emit8(s, 0x58); emit8(s, 0x48); emit8(s, 0x85); emit8(s, 0xC0);
        emit8(s, 0x0F); emit8(s, 0x84); 
        uint32_t patch_idx = s->text_idx; emit32(s, 0); 
        compile_statement(s);
        uint32_t else_offset = s->text_idx - (patch_idx + 4);
        *(uint32_t*)&s->text_section[patch_idx] = else_offset;
        if (match(s, "else")) {
            compile_statement(s); 
        }
    }
    else if (match(s, "for")) {
        match(s, "(");
        compile_statement(s); 
        int loop_start = s->text_idx;
        compile_expression(s); 
        match(s, ";");
        emit8(s, 0x58); emit8(s, 0x48); emit8(s, 0x85); emit8(s, 0xC0);
        emit8(s, 0x0F); emit8(s, 0x84); 
        uint32_t patch_end = s->text_idx; emit32(s, 0);
        char inc_var[32];
        parse_token(s, inc_var);
        if (match(s, "++")) { }
        match(s, ")");
        match(s, "{");
        while (!match(s, "}")) {
            compile_statement(s);
        }
        int off = find_symbol(s, inc_var);
        emit8(s, 0x48); emit8(s, 0xFF); emit8(s, 0x45); emit8(s, (uint8_t)(-off));
        emit8(s, 0xE9); emit32(s, loop_start - (s->text_idx + 4));
        *(uint32_t*)&s->text_section[patch_end] = s->text_idx - (patch_end + 4);
    }
    else if (match(s, "std::cout") || match(s, "cout")) compile_cout_chain(s);
    else if (match(s, "std::cerr") || match(s, "cerr")) compile_cout_chain(s); 
    else if (match(s, "return")) {
        parse_number(s); match(s, ";");
        emit8(s, 0xB8); emit32(s, 0); emit8(s, 0xCD); emit8(s, 0x80);
    }
    else if (match(s, "int")) {
        char name[32];
        parse_token(s, name);
        match(s, "=");
        compile_expression(s); 
        match(s, ";");
        // Variable remains on stack
        s->local_stack_size += 8;
        Symbol sym; strcpy(sym.name, name); sym.stack_offset = s->local_stack_size;
        s->symbols[s->symbol_count++] = sym;
    }
    else if (match(s, "{")) {
        while (!match(s, "}")) compile_statement(s);
    }
    else if (match(s, "using")) {
        while(*s->current != ';') s->current++;
        s->current++;
    }
    else {
        if (*s->current == ';') s->current++;
        else s->current++;
    }
}

void compile_main(CompilerState* s) {
    match(s, "int");
    match(s, "main");
    match(s, "(");
    
    // Prologue
    emit8(s, 0x55); // push rbp
    emit8(s, 0x48); emit8(s, 0x89); emit8(s, 0xE5); // mov rbp, rsp
    
    // Reserve Stack Space
    emit8(s, 0x48); emit8(s, 0x83); emit8(s, 0xEC); emit8(s, 0x80);
    
    if (match(s, "int")) {
        char argc_name[32]; parse_token(s, argc_name);
        match(s, ",");
        match(s, "char"); match(s, "**");
        char argv_name[32]; parse_token(s, argv_name);
        
        // Save args to reserved stack space
        s->local_stack_size += 8;
        // mov [rbp - 8], rdi (argc)
        emit8(s, 0x48); emit8(s, 0x89); emit8(s, 0x7D); emit8(s, (uint8_t)(-s->local_stack_size));
        Symbol sym1; strcpy(sym1.name, argc_name); sym1.stack_offset = s->local_stack_size;
        s->symbols[s->symbol_count++] = sym1;
        
        s->local_stack_size += 8;
        // mov [rbp - 16], rsi (argv)
        emit8(s, 0x48); emit8(s, 0x89); emit8(s, 0x75); emit8(s, (uint8_t)(-s->local_stack_size));
        Symbol sym2; strcpy(sym2.name, argv_name); sym2.stack_offset = s->local_stack_size;
        s->symbols[s->symbol_count++] = sym2;
    }
    
    match(s, ")");
    compile_statement(s); 
    
    emit8(s, 0xB8); emit32(s, 0); emit8(s, 0xCD); emit8(s, 0x80);
}

// --- UPDATED FORMAT: CXE (Chuckles Executable) ---
// Simple binary format: [Header] [Text Section] [Data Section]
struct CXEHeader {
    uint32_t magic;      // 0x00455843 ("CXE\0" Little Endian)
    uint32_t entry;      // Entry Point (Absolute Address)
    uint32_t text_len;   // Size of Text Section
    uint32_t data_len;   // Size of Data Section
};

void run_compiler(const char* in_file, const char* out_file) {
    printf("CCPP: Compiling %s...\n", in_file);
    CompilerState ctx;
    ctx.src = (char*)malloc(MAX_SRC);
    memset(ctx.src, 0, MAX_SRC);
    
    if (!Fat32::getInstance().read_file(in_file, ctx.src, MAX_SRC)) {
        printf("CCPP: Failed to read input file.\n");
        free(ctx.src);
        return;
    }
    
    process_includes(ctx.src, MAX_SRC);
    
    ctx.src_end = ctx.src + strlen(ctx.src);
    ctx.current = ctx.src;
    ctx.text_section = (uint8_t*)malloc(32768);
    ctx.data_section = (uint8_t*)malloc(32768);
    ctx.text_idx = 0; ctx.data_idx = 0;
    ctx.symbol_count = 0; ctx.local_stack_size = 0;
    
    compile_main(&ctx);
    
    // Create CXE Binary
    uint32_t total_size = sizeof(CXEHeader) + ctx.text_idx + ctx.data_idx;
    uint8_t* output_buf = (uint8_t*)malloc(total_size);
    if (!output_buf) {
        printf("CCPP: OOM output buffer.\n");
        free(ctx.src); free(ctx.text_section); free(ctx.data_section);
        return;
    }
    memset(output_buf, 0, total_size);

    CXEHeader* hdr = (CXEHeader*)output_buf;
    hdr->magic = 0x00455843; // "CXE\0"
    // The entry point matches what the compiler generated for absolute addresses
    hdr->entry = LOAD_BASE + 0x1000; 
    hdr->text_len = ctx.text_idx;
    hdr->data_len = ctx.data_idx;

    // Append Text
    memcpy(output_buf + sizeof(CXEHeader), ctx.text_section, ctx.text_idx);
    
    // Append Data
    memcpy(output_buf + sizeof(CXEHeader) + ctx.text_idx, ctx.data_section, ctx.data_idx);

    // Save
    if (Fat32::getInstance().write_file(out_file, output_buf, total_size)) {
        printf("CCPP: Successfully compiled to %s (%d bytes)\n", out_file, (int)total_size);
    } else {
        printf("CCPP: Failed to write output file.\n");
    }
    
    free(ctx.src); free(ctx.text_section); free(ctx.data_section); free(output_buf);
}