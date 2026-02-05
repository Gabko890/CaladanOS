; boot64.asm — 64-bit stub in LOW .boot.text (identity-mapped); jumps to higher-half kmain

BITS 64
default rel

SECTION .boot.text align=16
global long_mode_entry
extern kernel_main
extern __stack_bottom_hh     ; high end of reserved higher-half stack

extern mb_info
extern mb_magic

long_mode_entry:
    ; Reload segments (good hygiene in long mode)
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax

    ; Load the high end of the higher-half stack.
    ; The reserved stack grows downward from __stack_bottom_hh.
    mov     rsp, __stack_bottom_hh
    and     rsp, -16
    
    ;mov     edi, dword [mb_magic]
    ;mov     esi, dword [mb_info]

    mov rdi, [mb_magic]
    mov rsi, [mb_info]

    ; Enter C through a call so kernel_main sees the normal x86_64 ABI stack
    ; alignment (%rsp mod 16 == 8 on function entry).
    mov     rax, kernel_main
    call    rax

.halt:
    cli
    hlt
    jmp     .halt
