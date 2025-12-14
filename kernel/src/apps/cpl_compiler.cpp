#include "cpl_compiler.h"
#include "../fs/fat32.h"
#include "../memory/heap.h"
#include "../cppstd/string.h"
#include "../cppstd/stdio.h"
#include "../cppstd/stdlib.h"
#include "../input.h" 

#define MAX_SRC 65536
#define MAX_CODE 32768
#define HEADER_MAGIC "ChucklesProgram" 

enum VarType { T_INT, T_INT8, T_FLT, T_FLT4, T_CHAR, T_STR, T_PTR };

struct Variable {
    char name[32];
    VarType type;
    int stack_offset; 
};

struct RelocPatch {
    uint32_t code_offset_to_patch; 
    uint32_t data_target_idx;      
};

struct Block {
    int type; // 0=IF, 1=WHILE
    uint32_t start_addr; 
    uint32_t patch_addr; 
};

struct CompilerCtx {
    char* src;
    char* current;
    uint8_t* code;
    uint32_t code_idx;
    uint8_t* data;
    uint32_t data_idx;
    Variable vars[64];
    int var_count;
    int stack_size;
    RelocPatch patches[256];
    int patch_count;
    Block block_stack[32];
    int block_depth;
    bool error;
};

// --- Emitter ---
void emit8(CompilerCtx* ctx, uint8_t v) { 
    if (ctx->code_idx < MAX_CODE) ctx->code[ctx->code_idx++] = v; 
}
void emit32(CompilerCtx* ctx, uint32_t v) { 
    if (ctx->code_idx + 4 <= MAX_CODE) {
        *(uint32_t*)&ctx->code[ctx->code_idx] = v; 
        ctx->code_idx += 4; 
    }
}

// --- Helper: Emit Memory Access [RBP - offset] ---
// Automatically chooses disp8 or disp32
void emit_rbp_access(CompilerCtx* ctx, int reg_dest, int offset) {
    // 0x8B = MOV Reg, [Mem]
    // 0x03 = ADD Reg, [Mem]
    // We assume the opcode byte (e.g. 0x8B) was already emitted OR we handle it here?
    // Let's make this helper strictly for the ModRM + Disp part.
    
    // Actually, it's cleaner to handle the whole instruction if we can, 
    // but the opcode varies (MOV, ADD, LEA...).
    // So we will just emit ModRM + Displacement.
    
    // Reg = The register ID (0-7) being operated on.
    
    if (offset < 128) {
        // Mod=01 (disp8), RM=101 (RBP) -> 0x45
        emit8(ctx, 0x45 | (reg_dest << 3));
        emit8(ctx, (uint8_t)(-offset));
    } else {
        // Mod=10 (disp32), RM=101 (RBP) -> 0x85
        emit8(ctx, 0x85 | (reg_dest << 3));
        emit32(ctx, (uint32_t)(-offset));
    }
}

// --- Lexer & Utils ---
void skip_white(CompilerCtx* ctx) {
    while(*ctx->current && (*ctx->current <= ' ' || *ctx->current == '\n' || *ctx->current == '\r')) {
        ctx->current++;
    }
}

bool match(CompilerCtx* ctx, const char* str) {
    skip_white(ctx);
    int len = strlen(str);
    if (memcmp(ctx->current, str, len) == 0) {
        char last_char = str[len - 1];
        bool is_word_char = (last_char >= 'a' && last_char <= 'z') || (last_char >= 'A' && last_char <= 'Z') || (last_char >= '0' && last_char <= '9') || last_char == '_';
        if (is_word_char) {
            char next = ctx->current[len];
            if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9') || next == '_') return false;
        }
        ctx->current += len;
        return true;
    }
    return false;
}

void parse_token(CompilerCtx* ctx, char* out) {
    skip_white(ctx);
    int i = 0;
    while(i < 31 && ((*ctx->current >= 'a' && *ctx->current <= 'z') || (*ctx->current >= 'A' && *ctx->current <= 'Z') || *ctx->current == '_' || (*ctx->current >= '0' && *ctx->current <= '9'))) {
        out[i++] = *ctx->current++;
    }
    out[i] = 0;
}

int parse_int(CompilerCtx* ctx) {
    skip_white(ctx);
    int val = 0;
    bool neg = false;
    if (*ctx->current == '-') { neg = true; ctx->current++; }
    while(*ctx->current >= '0' && *ctx->current <= '9') {
        val = val * 10 + (*ctx->current - '0');
        ctx->current++;
    }
    return neg ? -val : val;
}

void parse_string(CompilerCtx* ctx, char* out_buf) {
    skip_white(ctx);
    if (*ctx->current == '"') ctx->current++;
    int i = 0;
    while (*ctx->current && *ctx->current != '"') {
        if (*ctx->current == '\\') {
            ctx->current++;
            if (*ctx->current == 'n') out_buf[i++] = '\n';
            else if (*ctx->current == 'v') { out_buf[i++] = '%'; ctx->current++; out_buf[i++] = 'd'; }
            else out_buf[i++] = *ctx->current;
        } else out_buf[i++] = *ctx->current;
        ctx->current++;
    }
    if (*ctx->current == '"') ctx->current++;
    out_buf[i] = 0;
}

int find_var(CompilerCtx* ctx, const char* name) {
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) return i;
    }
    return -1;
}

int get_reg_id(const char* name) {
    if (strcmp(name, "rax") == 0) return 0;
    if (strcmp(name, "rcx") == 0) return 1;
    if (strcmp(name, "rdx") == 0) return 2;
    if (strcmp(name, "rbx") == 0) return 3;
    if (strcmp(name, "rsp") == 0) return 4;
    if (strcmp(name, "rbp") == 0) return 5;
    if (strcmp(name, "rsi") == 0) return 6;
    if (strcmp(name, "rdi") == 0) return 7;
    return -1;
}

// --- Expressions ---
void compile_term(CompilerCtx* ctx) {
    skip_white(ctx);
    if (*ctx->current >= '0' && *ctx->current <= '9') {
        int val = parse_int(ctx);
        emit8(ctx, 0x48); emit8(ctx, 0xC7); emit8(ctx, 0xC0); emit32(ctx, val);
    } else {
        char name[32]; parse_token(ctx, name);
        if (strlen(name) > 0) {
            int vidx = find_var(ctx, name);
            if (vidx != -1) {
                // MOV RAX, [RBP - off]
                emit8(ctx, 0x48); emit8(ctx, 0x8B); 
                emit_rbp_access(ctx, 0, ctx->vars[vidx].stack_offset); // Dest=RAX(0)
            } else {
                printf("CPL Error: Unknown term '%s'\n", name);
                ctx->error = true;
            }
        }
    }
}

void compile_expression(CompilerCtx* ctx) {
    compile_term(ctx); 
    while (true) {
        skip_white(ctx);
        if (*ctx->current == 0) break;
        if (*ctx->current == '+') {
            ctx->current++; emit8(ctx, 0x50); compile_term(ctx); emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC3); emit8(ctx, 0x58); emit8(ctx, 0x48); emit8(ctx, 0x01); emit8(ctx, 0xD8); 
        } else if (*ctx->current == '-') {
            ctx->current++; emit8(ctx, 0x50); compile_term(ctx); emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC3); emit8(ctx, 0x58); emit8(ctx, 0x48); emit8(ctx, 0x29); emit8(ctx, 0xD8); 
        } else if (*ctx->current == '*') {
            ctx->current++; emit8(ctx, 0x50); compile_term(ctx); emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC3); emit8(ctx, 0x58); emit8(ctx, 0x48); emit8(ctx, 0x0F); emit8(ctx, 0xAF); emit8(ctx, 0xC3);
        } else break;
    }
}

// --- Logic Conditions ---
void compile_condition(CompilerCtx* ctx) {
    compile_expression(ctx); emit8(ctx, 0x50); 
    char op[3]; op[0] = *ctx->current++;
    if (*ctx->current == '=') { op[1] = '='; ctx->current++; op[2]=0; } else { op[1] = 0; }
    compile_expression(ctx); 
    emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC3); emit8(ctx, 0x58); 
    emit8(ctx, 0x48); emit8(ctx, 0x39); emit8(ctx, 0xD8);
    uint8_t jmp_op = 0;
    if (strcmp(op, "==") == 0) jmp_op = 0x85; 
    else if (strcmp(op, "!=") == 0) jmp_op = 0x84; 
    else if (strcmp(op, "<") == 0) jmp_op = 0x8D; 
    else if (strcmp(op, ">") == 0) jmp_op = 0x8E; 
    else if (strcmp(op, "<=") == 0) jmp_op = 0x8F; 
    else if (strcmp(op, ">=") == 0) jmp_op = 0x8C; 
    emit8(ctx, 0x0F); emit8(ctx, jmp_op);
}

// --- Control Flow ---
void compile_if(CompilerCtx* ctx) {
    match(ctx, "("); compile_condition(ctx); match(ctx, ")"); match(ctx, "{");
    ctx->block_stack[ctx->block_depth].type = 0; 
    ctx->block_stack[ctx->block_depth].patch_addr = ctx->code_idx;
    ctx->block_depth++;
    emit32(ctx, 0xAAAAAAAA); 
}

void compile_while(CompilerCtx* ctx) {
    uint32_t loop_start = ctx->code_idx;
    match(ctx, "("); compile_condition(ctx); match(ctx, ")"); match(ctx, "{");
    ctx->block_stack[ctx->block_depth].type = 1; 
    ctx->block_stack[ctx->block_depth].start_addr = loop_start;
    ctx->block_stack[ctx->block_depth].patch_addr = ctx->code_idx;
    ctx->block_depth++;
    emit32(ctx, 0xAAAAAAAA);
}

void close_block(CompilerCtx* ctx) {
    if (ctx->block_depth == 0) return;
    ctx->block_depth--;
    Block* b = &ctx->block_stack[ctx->block_depth];
    if (b->type == 1) { 
        emit8(ctx, 0xE9); 
        int32_t back_dist = b->start_addr - (ctx->code_idx + 4);
        emit32(ctx, back_dist);
    }
    uint32_t target = ctx->code_idx;
    int32_t fwd_dist = target - (b->patch_addr + 4);
    *(uint32_t*)&ctx->code[b->patch_addr] = fwd_dist;
}

// --- Inline ASM ---
void compile_asm_line(CompilerCtx* ctx) {
    skip_white(ctx);
    if (*ctx->current == 0) return;
    if (!((*ctx->current >= 'a' && *ctx->current <= 'z') || (*ctx->current >= 'A' && *ctx->current <= 'Z'))) {
        if (*ctx->current == '}') return;
        ctx->current++; return;
    }
    char opcode[16]; parse_token(ctx, opcode);
    if (strcmp(opcode, "inb") == 0 || strcmp(opcode, "inw") == 0 || strcmp(opcode, "inl") == 0) {
        skip_white(ctx);
        if (match(ctx, "al") || match(ctx, "ax") || match(ctx, "eax")) {
            match(ctx, ","); if (match(ctx, "dx")) {
                if (opcode[2]=='b') emit8(ctx, 0xEC);
                else if (opcode[2]=='w') { emit8(ctx, 0x66); emit8(ctx, 0xED); }
                else emit8(ctx, 0xED);
            }
        } else {
            int port = parse_int(ctx);
            emit8(ctx, 0x66); emit8(ctx, 0xBA); emit32(ctx, port); 
            if (opcode[2]=='b') emit8(ctx, 0xEC); 
            else if(opcode[2]=='w') { emit8(ctx, 0x66); emit8(ctx, 0xED); }
            else emit8(ctx, 0xED);
        }
    }
    else if (strcmp(opcode, "outb") == 0) {
        skip_white(ctx);
        if (match(ctx, "dx")) {
            match(ctx, ",");
            if (match(ctx, "al")) emit8(ctx, 0xEE); 
        } else {
            int port = parse_int(ctx);
            emit8(ctx, 0x66); emit8(ctx, 0xBA); emit32(ctx, port);
            match(ctx, ",");
            int val = parse_int(ctx); 
            emit8(ctx, 0xB0); emit8(ctx, val);
            emit8(ctx, 0xEE);
        }
    }
    else if (strcmp(opcode, "mov") == 0 || strcmp(opcode, "add") == 0 || strcmp(opcode, "sub") == 0) {
        char dest_str[16]; parse_token(ctx, dest_str); match(ctx, ",");
        int dest = get_reg_id(dest_str);
        skip_white(ctx);
        if (*ctx->current >= '0' && *ctx->current <= '9') {
            int imm = parse_int(ctx);
            if (strcmp(opcode, "mov") == 0) { emit8(ctx, 0x48); emit8(ctx, 0xC7); emit8(ctx, 0xC0 + dest); emit32(ctx, imm); }
            else if (strcmp(opcode, "add") == 0) { emit8(ctx, 0x48); emit8(ctx, 0x81); emit8(ctx, 0xC0 + dest); emit32(ctx, imm); }
            else if (strcmp(opcode, "sub") == 0) { emit8(ctx, 0x48); emit8(ctx, 0x81); emit8(ctx, 0xE8 + dest); emit32(ctx, imm); }
        } else {
            char src_str[32]; parse_token(ctx, src_str);
            int src_reg = get_reg_id(src_str);
            if (src_reg != -1) {
                if (strcmp(opcode, "mov") == 0) { emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC0 + (src_reg << 3) + dest); }
                else if (strcmp(opcode, "add") == 0) { emit8(ctx, 0x48); emit8(ctx, 0x01); emit8(ctx, 0xC0 + (src_reg << 3) + dest); }
            } else {
                int vidx = find_var(ctx, src_str);
                if (vidx != -1) {
                    if (strcmp(opcode, "mov") == 0) {
                        emit8(ctx, 0x48); emit8(ctx, 0x8B); emit_rbp_access(ctx, dest, ctx->vars[vidx].stack_offset);
                    } else if (strcmp(opcode, "add") == 0) {
                        emit8(ctx, 0x48); emit8(ctx, 0x03); emit_rbp_access(ctx, dest, ctx->vars[vidx].stack_offset);
                    }
                }
            }
        }
    }
    else if (strcmp(opcode, "int") == 0) { int vec = parse_int(ctx); emit8(ctx, 0xCD); emit8(ctx, (uint8_t)vec); }
    else if (strcmp(opcode, "ret") == 0) { emit8(ctx, 0xC3); }
    while (*ctx->current && *ctx->current != ';' && *ctx->current != '}' && *ctx->current != '\n') ctx->current++;
    if (*ctx->current == ';') ctx->current++;
}

// --- ABI Helpers ---
void emit_load_arg(CompilerCtx* ctx, int arg_idx, int val, bool is_var) {
    // Registers: 0=RDI, 1=RSI, 2=RDX, 3=RCX, 4=R8
    uint8_t reg_bits = 0;
    if (arg_idx == 0) reg_bits = 7; if (arg_idx == 1) reg_bits = 6; if (arg_idx == 2) reg_bits = 2; if (arg_idx == 3) reg_bits = 1; if (arg_idx == 4) reg_bits = 0;

    if (is_var) {
        if (arg_idx == 4) emit8(ctx, 0x4C); else emit8(ctx, 0x48);
        emit8(ctx, 0x8B); 
        emit_rbp_access(ctx, reg_bits, val); // Val is stack_offset here
    } else {
        if (arg_idx == 4) emit8(ctx, 0x49); else emit8(ctx, 0x48);
        emit8(ctx, 0xC7);
        emit8(ctx, 0xC0 | reg_bits); emit32(ctx, val);
    }
}

void compile_syscall(CompilerCtx* ctx, int syscall_id, int arg_count) {
    match(ctx, "(");
    int processed = 0;
    while (processed < arg_count) {
        if (processed > 0) match(ctx, ",");
        char* chk = ctx->current; char tag[16]; parse_token(ctx, tag); if (!match(ctx, "=")) ctx->current = chk; 
        skip_white(ctx); bool is_var = false; int val = 0;
        if (*ctx->current >= '0' && *ctx->current <= '9') val = parse_int(ctx);
        else {
            char var_name[32]; parse_token(ctx, var_name); int vidx = find_var(ctx, var_name);
            if (vidx != -1) { is_var = true; val = ctx->vars[vidx].stack_offset; }
        }
        emit_load_arg(ctx, processed, val, is_var); processed++;
    }
    match(ctx, ")"); match(ctx, ";");
    emit8(ctx, 0xB8); emit32(ctx, syscall_id); emit8(ctx, 0xCD); emit8(ctx, 0x80);
}

void compile_printf(CompilerCtx* ctx) {
    match(ctx, "("); skip_white(ctx);
    uint32_t str_data_offset = ctx->data_idx; ctx->current++; 
    while(*ctx->current && *ctx->current != '"') {
        if (*ctx->current == '\\' && *(ctx->current+1) == 'v') { ctx->data[ctx->data_idx++] = '%'; ctx->data[ctx->data_idx++] = 'd'; ctx->current += 3; }
        else if (*ctx->current == '\\' && *(ctx->current+1) == 'n') { ctx->data[ctx->data_idx++] = '\n'; ctx->current += 2; }
        else ctx->data[ctx->data_idx++] = *ctx->current++;
    }
    ctx->data[ctx->data_idx++] = 0; ctx->current++; 
    emit8(ctx, 0x48); emit8(ctx, 0x8D); emit8(ctx, 0x3D);
    ctx->patches[ctx->patch_count++] = { ctx->code_idx, str_data_offset }; emit32(ctx, 0xAAAAAAAA); 
    int processed_args = 0;
    while (match(ctx, ",")) {
        char tag[8]; parse_token(ctx, tag); match(ctx, "="); compile_expression(ctx); 
        int target_reg = processed_args + 1; 
        emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC0 + target_reg); 
        processed_args++;
    }
    match(ctx, ")"); match(ctx, ";"); emit8(ctx, 0xB8); emit32(ctx, 1); emit8(ctx, 0xCD); emit8(ctx, 0x80); 
}

void compile_var_decl(CompilerCtx* ctx, char* name) {
    bool new_var = false;
    if (match(ctx, "(")) { char type_name[16]; parse_token(ctx, type_name); match(ctx, ")"); new_var = true; }
    match(ctx, "=");
    
    if (match(ctx, "malloc")) {
        match(ctx, "("); int size = parse_int(ctx); match(ctx, ")"); match(ctx, ";");
        emit8(ctx, 0x48); emit8(ctx, 0xC7); emit8(ctx, 0xC7); emit32(ctx, size); 
        emit8(ctx, 0xB8); emit32(ctx, 6); emit8(ctx, 0xCD); emit8(ctx, 0x80); 
    }
    else if (match(ctx, "(") && match(ctx, "asm") && match(ctx, ",")) {
        char reg_name[8]; parse_token(ctx, reg_name); match(ctx, ")"); match(ctx, ";");
        int rid = get_reg_id(reg_name);
        emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xC0 + (rid<<3)); 
    }
    else if (match(ctx, "getch")) {
        match(ctx, "("); match(ctx, ")"); match(ctx, ";");
        emit8(ctx, 0xB8); emit32(ctx, 2); emit8(ctx, 0xCD); emit8(ctx, 0x80);
    } 
    else {
        skip_white(ctx);
        if (*ctx->current == '"') {
            char str_val[128]; parse_string(ctx, str_val);
            uint32_t doff = ctx->data_idx; strcpy((char*)&ctx->data[ctx->data_idx], str_val); ctx->data_idx += strlen(str_val) + 1;
            emit8(ctx, 0x48); emit8(ctx, 0x8D); emit8(ctx, 0x05);
            ctx->patches[ctx->patch_count++] = { ctx->code_idx, doff }; emit32(ctx, 0xAAAAAAAA);
        } else {
            compile_expression(ctx); match(ctx, ";");
        }
    }
    
    int off = 0;
    if (new_var) {
        ctx->stack_size += 8; Variable v; memset(&v, 0, sizeof(Variable)); strcpy(v.name, name); v.stack_offset = ctx->stack_size;
        ctx->vars[ctx->var_count++] = v; off = v.stack_offset;
    } else {
        int vidx = find_var(ctx, name);
        if (vidx != -1) off = ctx->vars[vidx].stack_offset;
    }
    
    // MOV [RBP - off], RAX (using safe helper)
    emit8(ctx, 0x48); emit8(ctx, 0x89); 
    emit_rbp_access(ctx, 0, off); // Src=RAX(0)
}

void compile_func(CompilerCtx* ctx) {
    char func_name[32]; parse_token(ctx, func_name);
    if (match(ctx, "(")) { while (*ctx->current && *ctx->current != ')') ctx->current++; match(ctx, ")"); }
    if (match(ctx, "[")) { while (*ctx->current && *ctx->current != ']') ctx->current++; match(ctx, "]"); }
    match(ctx, "{");
    emit8(ctx, 0x55); emit8(ctx, 0x48); emit8(ctx, 0x89); emit8(ctx, 0xE5);
    uint32_t stack_reserve_idx = ctx->code_idx;
    emit8(ctx, 0x48); emit8(ctx, 0x81); emit8(ctx, 0xEC); emit32(ctx, 0); 
    
    while (!match(ctx, "done") && *ctx->current != 0) {
        check_input_hooks(); 
        if (match(ctx, "printf")) compile_printf(ctx);
        else if (match(ctx, "sleep")) compile_syscall(ctx, 4, 1);
        else if (match(ctx, "drawrect")) compile_syscall(ctx, 5, 5);
        else if (match(ctx, "rmalloc")) compile_syscall(ctx, 7, 1); 
        else if (match(ctx, "disk_read")) compile_syscall(ctx, 8, 3);
        else if (match(ctx, "disk_write")) compile_syscall(ctx, 9, 3);
        else if (match(ctx, "if")) compile_if(ctx);
        else if (match(ctx, "while")) compile_while(ctx);
        else if (match(ctx, "}")) close_block(ctx);
        else if (match(ctx, "asm")) {
            if (match(ctx, "{")) { while (!match(ctx, "}") && *ctx->current != 0) compile_asm_line(ctx); } else compile_asm_line(ctx);
        }
        else {
            char name[32]; parse_token(ctx, name);
            if (name[0] == 0) { ctx->current++; continue; } 
            if (*ctx->current == '(' || *ctx->current == '=') compile_var_decl(ctx, name); else ctx->current++; 
        }
        skip_white(ctx);
    }
    match(ctx, "("); int ret_val = parse_int(ctx); match(ctx, ")"); match(ctx, ";"); match(ctx, "}");
    emit8(ctx, 0xB8); emit32(ctx, 0); emit8(ctx, 0xCD); emit8(ctx, 0x80); 
    uint32_t actual_stack = (ctx->stack_size + 16) & ~15;
    *(uint32_t*)&ctx->code[stack_reserve_idx + 3] = actual_stack;
}

void cpl_compile(const char* in_file, const char* out_file) {
    printf("CPL: Compiling %s...\n", in_file);
    CompilerCtx ctx; memset(&ctx, 0, sizeof(CompilerCtx));
    ctx.src = (char*)malloc(MAX_SRC); ctx.code = (uint8_t*)malloc(MAX_CODE); ctx.data = (uint8_t*)malloc(MAX_CODE);
    memset(ctx.src, 0, MAX_SRC); memset(ctx.code, 0, MAX_CODE); memset(ctx.data, 0, MAX_CODE);
    if (!Fat32::getInstance().read_file(in_file, ctx.src, MAX_SRC)) { printf("CPL: Read Error.\n"); free(ctx.src); return; }
    ctx.current = ctx.src;
    while (match(&ctx, ".header")) { ctx.current++; while(*ctx.current && *ctx.current != '>') ctx.current++; ctx.current++; }
    while (*ctx.current) { if (match(&ctx, "func")) compile_func(&ctx); else ctx.current++; }
    if (!ctx.error) {
        for(int i=0; i<ctx.patch_count; i++) {
            uint32_t inst_end = ctx.patches[i].code_offset_to_patch + 4;
            uint32_t data_loc = ctx.code_idx + ctx.patches[i].data_target_idx;
            *(int32_t*)&ctx.code[ctx.patches[i].code_offset_to_patch] = data_loc - inst_end;
        }
        uint32_t sz = 16 + ctx.code_idx + ctx.data_idx;
        uint8_t* bin = (uint8_t*)malloc(sz); memset(bin, 0, sz);
        memcpy(bin, HEADER_MAGIC, strlen(HEADER_MAGIC));
        memcpy(bin + 16, ctx.code, ctx.code_idx);
        memcpy(bin + 16 + ctx.code_idx, ctx.data, ctx.data_idx);
        Fat32::getInstance().write_file(out_file, bin, sz);
        free(bin);
        printf("CPL: Build Complete.\n");
    }
    free(ctx.src); free(ctx.code); free(ctx.data);
}