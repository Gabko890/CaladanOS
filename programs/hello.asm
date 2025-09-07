; hello.asm - Simple test program that uses syscalls
; Assembles to a relocatable object file (.o)

section .rodata
msg:    db 'Hello from loaded ELF REL program!', 10, 0
msg_len equ $ - msg - 1

section .text
global _start

_start:
    ; sys_write(1, msg, msg_len)
    mov rax, 4          ; syscall number for write
    mov rdi, 1          ; fd = stdout
    mov rsi, msg        ; buffer
    mov rdx, msg_len    ; count
    int 0x80            ; invoke syscall
    
    ; sys_exit(42) - this should terminate the process and return to shell
    mov rax, 1          ; syscall number for exit
    mov rdi, 42         ; exit status
    int 0x80            ; invoke syscall
    
    ; sys_exit should never return, but just in case:
    mov rax, 0          ; fallback return value
    ret                 ; return to ELF executor