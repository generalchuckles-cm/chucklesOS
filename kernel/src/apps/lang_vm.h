#ifndef LANG_VM_H
#define LANG_VM_H

#include <cstdint>

// Opcodes for our Virtual Machine
enum Opcode {
    OP_PUSH_INT,   // Push 64-bit integer
    OP_PUSH_STR,   // Push string index
    OP_POP,        // Pop top of stack
    
    // Math & Logic
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_LT, OP_GT,
    
    // Variables
    OP_STORE_VAR,  // Pop value, store in var index
    OP_LOAD_VAR,   // Push value from var index
    
    // Control Flow
    OP_JMP,        // Unconditional jump to PC
    OP_JMP_FALSE,  // Pop condition, jump if 0
    
    // I/O & Memory
    OP_PRINTF,     // Pop string idx, print
    OP_PRINT_INT,  // Pop int, print
    OP_PEEK,       // Pop address, push value at address
    OP_POKE,       // Pop value, Pop address, write value to address
    OP_MALLOC,     // Pop size, push allocated address
    OP_FREE,       // Pop address, free it
    
    OP_EXIT,       // Pop exit code, end
    OP_HALT        // Natural end of program
};

class LangVM {
public:
    LangVM();
    ~LangVM();

    bool run_script(const char* source);

private:
    uint8_t* bytecode;
    int bytecode_size;
    
    char* string_pool[128];
    int num_strings;
    
    struct Var {
        char name[32];
        int idx;
    };
    Var variables[256];
    int num_vars;

    // Compiler State
    const char* src_ptr;
    
    struct Token {
        int type;
        const char* start;
        int length;
    };
    Token current_token;

    // Pipeline
    void advance();
    bool match(int type);
    void consume(int type, const char* err);
    
    int emit_byte(uint8_t byte);
    int emit_int64(uint64_t val);
    int emit_int32(uint32_t val);
    void patch_int32(int offset, uint32_t val);
    
    int add_string(const char* start, int len);
    int resolve_var(const char* start, int len);
    
    // Recursive Descent Parser
    void parse_primary();
    void parse_factor();
    void parse_term();
    void parse_comparison();
    void parse_expr();
    void parse_statement();
    void parse_block();
    bool compile();
    
    void execute();
    
    // Helpers
    bool is_alpha(char c);
    bool is_digit(char c);
    uint64_t parse_int(const char* start, int len);
    bool token_equals(const Token& t, const char* str);
};

#endif