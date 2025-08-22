global long_mode_start
extern kernel_main
extern multiboot_magic
extern multiboot_info

section .text
bits 64
long_mode_start:
    ; load null into all data segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    

    mov rsi, [multiboot_info]
    mov rdi, [multiboot_magic]
	call kernel_main
    
    hlt
