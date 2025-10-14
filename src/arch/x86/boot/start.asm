; 32-bit Multiboot2 entry stub in NASM syntax

BITS 32

global _start
extern kmain_regs

section .bss align=16
stack_bottom:
    resb 16384
stack_top:

section .text
_start:
    cli
    mov esp, stack_top
    mov ebp, esp
    ; Multiboot2 passes magic in EAX, info address in EBX
    ; Bridge registers -> C call (cdecl): push info, then magic
    mov ecx, eax
    mov edx, ebx
    push edx
    push ecx
    call kmain_regs
.hang:
    cli
    hlt
    jmp .hang
