; boot64.asm — 64-bit stub in LOW .boot.text (identity-mapped); jumps to higher-half kmain

BITS 64
default rel

SECTION .boot.text align=16
global long_mode_entry
extern kernel_main
extern __stack_top_hh        ; from linker.ld

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

    ; Load the higher-half stack using a full 64-bit immediate
    mov     rsp, __stack_top_hh
    
    ;mov     edi, dword [mb_magic]
    ;mov     esi, dword [mb_info]

    mov rdi, [mb_magic]
    mov rsi, [mb_info]

    ; Jump to higher-half C entry with a 64-bit absolute jump
    mov     rax, kernel_main
    jmp     rax
