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

global jump_to_user_program
global restore_kernel_stack
global load_gdt
global call_kernel_program

section .data
    saved_kernel_rsp: dq 0

section .text

; --- NEW FUNCTION: Safely call kernel code with exit trap ---
; void call_kernel_program(void* entry_point)
call_kernel_program:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; 1. Setup Safe Return Address
    ; We push the label .safe_exit onto the stack.
    ; If restore_kernel_stack is called, it loads 'saved_kernel_rsp',
    ; which points here, and then executes RET, which pops .safe_exit.
    lea rax, [.safe_exit]
    push rax
    
    ; 2. Save this specific stack location
    mov [saved_kernel_rsp], rsp
    
    ; 3. Call the raw binary
    call rdi
    
    ; 4. If program returns normally (ret), it pops the .safe_exit we pushed above.
    ; So we fall through here.
    
.safe_exit:
    ; Restore environment
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

jump_to_user_program:
    cli  
    mov [saved_kernel_rsp], rsp
    
    mov r12, rdi    ; entry
    mov r13, rsi    ; argc
    mov r14, rdx    ; argv
    mov r15, rcx    ; stack_top
    
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push 0x23           ; SS
    push r15            ; RSP
    
    pushfq
    pop rax
    or rax, 0x200       ; Enable IF
    push rax            ; RFLAGS
    
    push 0x1B           ; CS
    push r12            ; RIP
    
    mov rdi, r13        ; argc
    mov rsi, r14        ; argv
    xor rdx, rdx        
    
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    iretq

restore_kernel_stack:
    ; This loads the RSP we saved in call_kernel_program
    mov rsp, [saved_kernel_rsp]
    ; Returns to .safe_exit
    ret

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