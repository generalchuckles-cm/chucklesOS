bits 64

extern exception_handler
extern irq_handler
extern cpp_syscall_handler

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0                  
    push %1                 
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1                 
    jmp isr_common_stub
%endmacro

; --- Exception ISRs ---
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

; --- IRQ ISRs ---
ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47

; --- Syscall Entry (INT 0x80) ---
global isr128
isr128:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; System V ABI Register Shuffle
    mov r9, r8
    mov r8, rcx
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    
    call cpp_syscall_handler
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    iretq

; --- Helper to load GDT ---
global load_gdt
load_gdt:
    lgdt [rdi]
    mov es, dx
    mov ds, dx
    mov fs, dx
    mov gs, dx
    mov ss, dx
    push rsi
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    ltr cx
    ret

; --- Common ISR Stub ---
isr_common_stub:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    
    mov rdi, rsp 
    mov rax, [rsp + 120] 
    cmp rax, 32
    jae .is_irq
    
    call exception_handler
    jmp .exit
    
.is_irq:
    call irq_handler
    
.exit:
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq