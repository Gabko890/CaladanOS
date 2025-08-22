section .text
global irq1_handler
global default_handler
extern handle_ps2

irq1_handler:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Call your C handler
    call handle_ps2
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Return from interrupt
    iretq

default_handler:
    ; Send EOI to master PIC for any unhandled interrupt
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    iretq
