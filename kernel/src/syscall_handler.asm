; syscall_handler.asm - Assembly wrapper for 0x80 syscall interrupt
section .text

extern syscall_dispatch

global syscall_interrupt_handler

; Debug console output helper
%macro DEBUG_CHAR 1
    push rax
    push rdx
    mov al, %1
    mov dx, 0xe9        ; QEMU debug console port
    out dx, al
    pop rdx
    pop rax
%endmacro

; 0x80 interrupt handler
; Syscall number in rax
; Arguments in rdi, rsi, rdx, rcx, r8, r9 (System V ABI)
syscall_interrupt_handler:
    ; Debug: Write '[' to debug console to show we entered
    DEBUG_CHAR '['
    
    ; Save all caller-saved registers
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    
    ; Set up arguments for syscall_dispatch(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6)
    ; syscall_num (from rax) goes to rdi
    ; arg1 (from rdi) goes to rsi
    ; arg2 (from rsi) goes to rdx
    ; arg3 (from rdx) goes to rcx
    ; arg4 (from rcx) goes to r8
    ; arg5 (from r8) goes to r9
    ; arg6 (from r9) goes to stack
    
    push r9         ; arg6 on stack
    mov r9, r8      ; arg5
    mov r8, rcx     ; arg4
    mov rcx, rdx    ; arg3
    mov rdx, rsi    ; arg2
    mov rsi, rdi    ; arg1
    mov rdi, rax    ; syscall_num
    
    ; Debug: Write 'C' to show we're about to call C dispatcher
    DEBUG_CHAR 'C'
    
    ; Call the C dispatcher
    call syscall_dispatch
    
    ; Debug: Write 'R' to show C dispatcher returned
    DEBUG_CHAR 'R'
    
    ; Clean up stack (arg6)
    add rsp, 8
    
    ; Return value is in rax - keep it there
    
    ; Restore caller-saved registers (except rax which has return value)
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    add rsp, 8      ; Skip saved rax - return value is already in rax
    
    ; Debug: Write ']' to show we're about to iretq
    DEBUG_CHAR ']'
    
    ; Return from interrupt
    iretq