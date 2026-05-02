#include "lang_vm.h"
#include "../cppstd/stdio.h"
#include "../cppstd/string.h"
#include "../memory/heap.h"

enum TokenType {
    TOK_EOF, TOK_IDENT, TOK_STRING, TOK_NUMBER,
    TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN,
    TOK_SEMI, TOK_COMMA, TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_EQEQ, TOK_LT, TOK_GT, TOK_ERROR
};

LangVM::LangVM() {
    bytecode = (uint8_t*)malloc(65536); // 64KB bytecode buffer
    bytecode_size = 0;
    num_strings = 0;
    num_vars = 0;
}

LangVM::~LangVM() {
    if (bytecode) free(bytecode);
    for (int i = 0; i < num_strings; i++) free(string_pool[i]);
}

bool LangVM::is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool LangVM::is_digit(char c) { return (c >= '0' && c <= '9'); }
uint64_t LangVM::parse_int(const char* start, int len) {
    uint64_t v = 0;
    for (int i = 0; i < len; i++) v = v * 10 + (start[i] - '0');
    return v;
}
bool LangVM::token_equals(const Token& t, const char* str) {
    if ((size_t)t.length != strlen(str)) return false;
    for (int i = 0; i < t.length; i++) if (t.start[i] != str[i]) return false;
    return true;
}

void LangVM::advance() {
    while (true) {
        while (*src_ptr == ' ' || *src_ptr == '\n' || *src_ptr == '\t' || *src_ptr == '\r') src_ptr++;
        if (*src_ptr == '#') while (*src_ptr && *src_ptr != '\n') src_ptr++;
        else break;
    }

    if (!*src_ptr) { current_token = { TOK_EOF, src_ptr, 0 }; return; }

    char c = *src_ptr;
    const char* start = src_ptr;

    if (c == '{') { src_ptr++; current_token = { TOK_LBRACE, start, 1 }; return; }
    if (c == '}') { src_ptr++; current_token = { TOK_RBRACE, start, 1 }; return; }
    if (c == '(') { src_ptr++; current_token = { TOK_LPAREN, start, 1 }; return; }
    if (c == ')') { src_ptr++; current_token = { TOK_RPAREN, start, 1 }; return; }
    if (c == ';') { src_ptr++; current_token = { TOK_SEMI, start, 1 }; return; }
    if (c == ',') { src_ptr++; current_token = { TOK_COMMA, start, 1 }; return; }
    if (c == '+') { src_ptr++; current_token = { TOK_PLUS, start, 1 }; return; }
    if (c == '-') { src_ptr++; current_token = { TOK_MINUS, start, 1 }; return; }
    if (c == '*') { src_ptr++; current_token = { TOK_STAR, start, 1 }; return; }
    if (c == '/') { src_ptr++; current_token = { TOK_SLASH, start, 1 }; return; }
    if (c == '%') { src_ptr++; current_token = { TOK_PERCENT, start, 1 }; return; }
    if (c == '<') { src_ptr++; current_token = { TOK_LT, start, 1 }; return; }
    if (c == '>') { src_ptr++; current_token = { TOK_GT, start, 1 }; return; }
    if (c == '=') {
        src_ptr++;
        if (*src_ptr == '=') { src_ptr++; current_token = { TOK_EQEQ, start, 2 }; return; }
        current_token = { TOK_EQ, start, 1 }; return;
    }
    if (c == '"') {
        src_ptr++; start = src_ptr;
        while (*src_ptr && *src_ptr != '"') src_ptr++;
        current_token = { TOK_STRING, start, (int)(src_ptr - start) };
        if (*src_ptr == '"') src_ptr++;
        return;
    }
    if (is_digit(c)) {
        while (is_digit(*src_ptr)) src_ptr++;
        current_token = { TOK_NUMBER, start, (int)(src_ptr - start) }; return;
    }
    if (is_alpha(c)) {
        while (is_alpha(*src_ptr) || is_digit(*src_ptr)) src_ptr++;
        current_token = { TOK_IDENT, start, (int)(src_ptr - start) }; return;
    }
    src_ptr++; current_token = { TOK_ERROR, start, 1 };
}

bool LangVM::match(int type) {
    if (current_token.type == type) { advance(); return true; }
    return false;
}

void LangVM::consume(int type, const char* err) {
    if (current_token.type == type) advance();
    else printf("[VM Compiler Error] %s\n", err);
}

int LangVM::emit_byte(uint8_t byte) {
    bytecode[bytecode_size] = byte;
    return bytecode_size++;
}

int LangVM::emit_int64(uint64_t val) {
    int offset = bytecode_size;
    *(uint64_t*)(&bytecode[bytecode_size]) = val;
    bytecode_size += 8;
    return offset;
}

int LangVM::emit_int32(uint32_t val) {
    int offset = bytecode_size;
    *(uint32_t*)(&bytecode[bytecode_size]) = val;
    bytecode_size += 4;
    return offset;
}

void LangVM::patch_int32(int offset, uint32_t val) {
    *(uint32_t*)(&bytecode[offset]) = val;
}

int LangVM::add_string(const char* start, int len) {
    char* str = (char*)malloc(len + 1);
    int j = 0;
    
    // Parse escape sequences (\n, \t, etc)
    for (int i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++; // Skip the slash
            if (start[i] == 'n') str[j++] = '\n';
            else if (start[i] == 't') str[j++] = '\t';
            else if (start[i] == 'r') str[j++] = '\r';
            else if (start[i] == '\\') str[j++] = '\\';
            else if (start[i] == '"') str[j++] = '"';
            else { 
                str[j++] = '\\'; 
                str[j++] = start[i]; 
            }
        } else {
            str[j++] = start[i];
        }
    }
    str[j] = '\0';
    
    string_pool[num_strings] = str;
    return num_strings++;
}

int LangVM::resolve_var(const char* start, int len) {
    for (int i = 0; i < num_vars; i++) {
        if ((size_t)len == strlen(variables[i].name) && memcmp(variables[i].name, start, len) == 0) return i;
    }
    // New variable
    memcpy(variables[num_vars].name, start, len);
    variables[num_vars].name[len] = '\0';
    return num_vars++;
}

void LangVM::parse_primary() {
    if (current_token.type == TOK_NUMBER) {
        uint64_t val = parse_int(current_token.start, current_token.length);
        emit_byte(OP_PUSH_INT); emit_int64(val);
        advance();
    } else if (current_token.type == TOK_STRING) {
        int idx = add_string(current_token.start, current_token.length);
        emit_byte(OP_PUSH_STR); emit_int32(idx);
        advance();
    } else if (current_token.type == TOK_IDENT) {
        Token id = current_token;
        advance();
        if (token_equals(id, "peek")) {
            consume(TOK_LPAREN, "Expected '(' after peek");
            parse_expr();
            consume(TOK_RPAREN, "Expected ')' after peek arguments");
            emit_byte(OP_PEEK);
        } else if (token_equals(id, "malloc")) {
            consume(TOK_LPAREN, "Expected '(' after malloc");
            parse_expr();
            consume(TOK_RPAREN, "Expected ')' after malloc arguments");
            emit_byte(OP_MALLOC);
        } else {
            // Variable load
            int var_idx = resolve_var(id.start, id.length);
            emit_byte(OP_LOAD_VAR); emit_int32(var_idx);
        }
    } else if (match(TOK_LPAREN)) {
        parse_expr();
        consume(TOK_RPAREN, "Expected ')' after expression");
    }
}

void LangVM::parse_factor() {
    parse_primary();
    while (current_token.type == TOK_STAR || current_token.type == TOK_SLASH || current_token.type == TOK_PERCENT) {
        int op = current_token.type; advance();
        parse_primary();
        if (op == TOK_STAR) emit_byte(OP_MUL);
        else if (op == TOK_SLASH) emit_byte(OP_DIV);
        else if (op == TOK_PERCENT) emit_byte(OP_MOD);
    }
}

void LangVM::parse_term() {
    parse_factor();
    while (current_token.type == TOK_PLUS || current_token.type == TOK_MINUS) {
        int op = current_token.type; advance();
        parse_factor();
        if (op == TOK_PLUS) emit_byte(OP_ADD);
        else emit_byte(OP_SUB);
    }
}

void LangVM::parse_comparison() {
    parse_term();
    while (current_token.type == TOK_EQEQ || current_token.type == TOK_LT || current_token.type == TOK_GT) {
        int op = current_token.type; advance();
        parse_term();
        if (op == TOK_EQEQ) emit_byte(OP_EQ);
        else if (op == TOK_LT) emit_byte(OP_LT);
        else emit_byte(OP_GT);
    }
}

void LangVM::parse_expr() { parse_comparison(); }

void LangVM::parse_statement() {
    if (current_token.type == TOK_IDENT) {
        Token id = current_token;
        if (token_equals(id, "var")) {
            advance(); // Eat 'var'
            Token var_name = current_token;
            consume(TOK_IDENT, "Expected variable name");
            consume(TOK_EQ, "Expected '=' in var declaration");
            parse_expr();
            consume(TOK_SEMI, "Expected ';' after var declaration");
            int idx = resolve_var(var_name.start, var_name.length);
            emit_byte(OP_STORE_VAR); emit_int32(idx);
        } else if (token_equals(id, "if")) {
            advance(); // Eat 'if'
            consume(TOK_LPAREN, "Expected '(' after if");
            parse_expr();
            consume(TOK_RPAREN, "Expected ')' after condition");
            
            emit_byte(OP_JMP_FALSE);
            int jump_false = emit_int32(0); // Hole
            
            parse_block();
            
            if (current_token.type == TOK_IDENT && token_equals(current_token, "else")) {
                advance(); // Eat 'else'
                emit_byte(OP_JMP);
                int jump_end = emit_int32(0); // Hole
                patch_int32(jump_false, bytecode_size); // Patch if false to jump to else
                parse_block();
                patch_int32(jump_end, bytecode_size); // Patch else end
            } else {
                patch_int32(jump_false, bytecode_size); // Patch if false to jump past block
            }
        } else if (token_equals(id, "while")) {
            advance(); // Eat 'while'
            int loop_start = bytecode_size;
            consume(TOK_LPAREN, "Expected '(' after while");
            parse_expr();
            consume(TOK_RPAREN, "Expected ')' after condition");
            
            emit_byte(OP_JMP_FALSE);
            int jump_end = emit_int32(0); // Hole
            
            parse_block();
            
            emit_byte(OP_JMP); emit_int32(loop_start);
            patch_int32(jump_end, bytecode_size); // Patch loop exit
            
        } else if (token_equals(id, "printf")) {
            advance(); consume(TOK_LPAREN, "Expected '('");
            parse_expr(); // Should result in string push
            consume(TOK_RPAREN, "Expected ')'"); consume(TOK_SEMI, "Expected ';'");
            emit_byte(OP_PRINTF);
        } else if (token_equals(id, "print_int")) {
            advance(); consume(TOK_LPAREN, "Expected '('");
            parse_expr();
            consume(TOK_RPAREN, "Expected ')'"); consume(TOK_SEMI, "Expected ';'");
            emit_byte(OP_PRINT_INT);
        } else if (token_equals(id, "poke")) {
            advance(); consume(TOK_LPAREN, "Expected '('");
            parse_expr(); // ptr
            consume(TOK_COMMA, "Expected ','");
            parse_expr(); // value
            consume(TOK_RPAREN, "Expected ')'"); consume(TOK_SEMI, "Expected ';'");
            emit_byte(OP_POKE);
        } else if (token_equals(id, "free")) {
            advance(); consume(TOK_LPAREN, "Expected '('");
            parse_expr(); // ptr
            consume(TOK_RPAREN, "Expected ')'"); consume(TOK_SEMI, "Expected ';'");
            emit_byte(OP_FREE);
        } else if (token_equals(id, "exit")) {
            advance(); parse_expr(); consume(TOK_SEMI, "Expected ';'"); emit_byte(OP_EXIT);
        } else {
            // Must be assignment
            advance(); // Eat Ident
            consume(TOK_EQ, "Expected '=' in assignment");
            parse_expr();
            consume(TOK_SEMI, "Expected ';' after assignment");
            int idx = resolve_var(id.start, id.length);
            emit_byte(OP_STORE_VAR); emit_int32(idx);
        }
    } else {
        printf("[VM Compiler] Unexpected token.\n"); advance();
    }
}

void LangVM::parse_block() {
    consume(TOK_LBRACE, "Expected '{'");
    while (current_token.type != TOK_RBRACE && current_token.type != TOK_EOF) {
        parse_statement();
    }
    consume(TOK_RBRACE, "Expected '}'");
}

bool LangVM::compile() {
    advance(); // Load first token
    
    // Ignore optional library header
    if (current_token.type == TOK_IDENT && token_equals(current_token, "library")) {
        advance(); match(TOK_LT); advance(); match(TOK_GT);
    }
    
    if (current_token.type == TOK_IDENT && token_equals(current_token, "function")) {
        advance(); advance(); // Eat 'function main'
        parse_block();
        emit_byte(OP_HALT);
        return true;
    }
    return false;
}

void LangVM::execute() {
    uint64_t stack[512]; // 64-bit stack so pointers fit safely
    uint64_t vars[256] = {0};
    int sp = 0;
    int ip = 0;

    while (ip < bytecode_size) {
        uint8_t op = bytecode[ip++];
        
        switch (op) {
            case OP_PUSH_INT: stack[sp++] = *(uint64_t*)(&bytecode[ip]); ip += 8; break;
            case OP_PUSH_STR: stack[sp++] = *(uint32_t*)(&bytecode[ip]); ip += 4; break;
            case OP_POP: sp--; break;
            
            case OP_ADD: { uint64_t b=stack[--sp]; uint64_t a=stack[--sp]; stack[sp++] = a+b; break; }
            case OP_SUB: { uint64_t b=stack[--sp]; uint64_t a=stack[--sp]; stack[sp++] = a-b; break; }
            case OP_MUL: { uint64_t b=stack[--sp]; uint64_t a=stack[--sp]; stack[sp++] = a*b; break; }
            case OP_DIV: { uint64_t b=stack[--sp]; uint64_t a=stack[--sp]; stack[sp++] = b==0?0:a/b; break; }
            case OP_MOD: { uint64_t b=stack[--sp]; uint64_t a=stack[--sp]; stack[sp++] = b==0?0:a%b; break; }
            case OP_EQ:  { uint64_t b=stack[--sp]; uint64_t a=stack[--sp]; stack[sp++] = (a==b)?1:0; break; }
            case OP_LT:  { int64_t b=(int64_t)stack[--sp]; int64_t a=(int64_t)stack[--sp]; stack[sp++] = (a<b)?1:0; break; }
            case OP_GT:  { int64_t b=(int64_t)stack[--sp]; int64_t a=(int64_t)stack[--sp]; stack[sp++] = (a>b)?1:0; break; }
            
            case OP_STORE_VAR: { uint32_t idx = *(uint32_t*)(&bytecode[ip]); ip+=4; vars[idx] = stack[--sp]; break; }
            case OP_LOAD_VAR:  { uint32_t idx = *(uint32_t*)(&bytecode[ip]); ip+=4; stack[sp++] = vars[idx]; break; }
            
            case OP_JMP:       { ip = *(uint32_t*)(&bytecode[ip]); break; }
            case OP_JMP_FALSE: { uint32_t addr = *(uint32_t*)(&bytecode[ip]); ip+=4; if(!stack[--sp]) ip = addr; break; }
            
            case OP_PRINTF:    { uint32_t str_idx = stack[--sp]; printf("%s", string_pool[str_idx]); break; }
            case OP_PRINT_INT: { int64_t val = (int64_t)stack[--sp]; printf("%d", (int)val); break; }
            
            case OP_PEEK:      { uint64_t addr = stack[--sp]; stack[sp++] = *(uint64_t*)addr; break; }
            case OP_POKE:      { uint64_t val = stack[--sp]; uint64_t addr = stack[--sp]; *(uint64_t*)addr = val; break; }
            case OP_MALLOC:    { uint64_t size = stack[--sp]; stack[sp++] = (uint64_t)malloc(size); break; }
            case OP_FREE:      { uint64_t addr = stack[--sp]; free((void*)addr); break; }
            
            case OP_EXIT:      { int code = (int)stack[--sp]; printf("\n[VM] Exited with code %d\n", code); return; }
            case OP_HALT:      return;
        }
    }
}

bool LangVM::run_script(const char* source) {
    src_ptr = source;
    bytecode_size = 0; num_strings = 0; num_vars = 0;
    if (!compile()) { printf("Compilation failed.\n"); return false; }
    execute();
    return true;
}