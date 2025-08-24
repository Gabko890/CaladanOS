; boot32.asm — 32-bit stub: build paging for identity + higher half, enable long mode, far-jump to 64-bit stub
; assemble: nasm -felf64 boot32.asm -o boot32.o   (elf64 obj is fine; we emit 32-bit code with BITS 32)
; entry: _start  (placed in low .boot.text by linker.ld)

BITS 32
default rel

%define KERNEL_PMA         0x00200000
%define KERNEL_VMA         0xFFFFFFFF80000000

%define PML4_BASE          0x00009000
%define PDPT_LO_BASE       0x0000A000
%define PD_LO_BASE         0x0000B000
%define PDPT_HI_BASE       0x0000C000
%define PD_HI_BASE         0x0000D000

%define CR0_PG             (1 << 31)
%define CR4_PAE            (1 << 5)
%define EFER_MSR           0xC0000080
%define EFER_LME           (1 << 8)

; how many 2MiB pages to map for the kernel higher-half (increase if your kernel > 2MiB)
%define KERNEL_2M_PAGES    2

SECTION .boot.text align=16
global _start
extern long_mode_entry       ; defined in boot64.asm (also placed in .boot.text at low VMA)

global mb_info
global mb_magic


_start:
    cli

    mov [mb_magic], eax
    mov [mb_info], ebx

    ; ---------------------------
    ; Minimal GDT with 64-bit code & data
    ; ---------------------------
    lgdt    [gdt_ptr]

    ; Make sure CS reload happens via far jump later; set data segments now
    mov     ax, 0x10            ; data selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; ---------------------------
    ; Build paging structures
    ; ---------------------------
    call    build_paging

    ; Load CR3 with PML4 physical
    mov     eax, PML4_BASE
    mov     cr3, eax

    ; Enable PAE
    mov     eax, cr4
    or      eax, CR4_PAE
    mov     cr4, eax

    ; Enable LME in EFER
    mov     ecx, EFER_MSR
    rdmsr
    or      eax, EFER_LME
    wrmsr

    ; Enable paging
    mov     eax, cr0
    or      eax, CR0_PG
    mov     cr0, eax

    ; Far jump to 64-bit code segment (selector 0x08) at a LOW address
    ; long_mode_entry is linked into .boot.text (low, identity mapped)
    jmp     0x08:long_mode_entry

; -------------------------------------------------------
; Build minimal x86-64 paging:
; - Identity map first 4 MiB via 2MiB pages (PD_LO[0]=0MiB, PD_LO[1]=2MiB)
; - Higher-half map KERNEL_PMA.. via PD_HI entries at 0xFFFFFFFF80000000
; -------------------------------------------------------
build_paging:
    ; Zero out tables (we'll just do a quick 4 pages worth)
    ; Use rep stosd in 32-bit mode over 20 KiB safely (crude but fine)
    push    edi
    push    ecx
    push    eax

    xor     eax, eax
    mov     edi, PML4_BASE
    mov     ecx, (5*4096)/4      ; PML4 + two PDPT + two PD = 5 pages
    rep stosd

    ; PML4[0] -> PDPT_LO
    mov     eax, PDPT_LO_BASE | 0x03
    mov     dword [PML4_BASE + 0*8 + 0], eax
    mov     dword [PML4_BASE + 0*8 + 4], 0

    ; PML4[511] -> PDPT_HI
    mov     eax, PDPT_HI_BASE | 0x03
    mov     dword [PML4_BASE + 511*8 + 0], eax
    mov     dword [PML4_BASE + 511*8 + 4], 0

    ; PDPT_LO[0] -> PD_LO
    mov     eax, PD_LO_BASE | 0x03
    mov     dword [PDPT_LO_BASE + 0*8 + 0], eax
    mov     dword [PDPT_LO_BASE + 0*8 + 4], 0

    ; Identity map 0..4MiB via two 2MiB pages
    ; PD_LO[0] = 0MiB (PS=1)
    mov     eax, (0x00000000) | (1<<7) | 0x003
    mov     dword [PD_LO_BASE + 0*8 + 0], eax
    mov     dword [PD_LO_BASE + 0*8 + 4], 0

    ; PD_LO[1] = 2MiB (covers 0x200000..0x3FFFFF)
    mov     eax, (0x00200000) | (1<<7) | 0x003
    mov     dword [PD_LO_BASE + 1*8 + 0], eax
    mov     dword [PD_LO_BASE + 1*8 + 4], 0

    ; PDPT_HI[510] -> PD_HI  (VMA 0xFFFFFFFF80000000 indexes)
    mov     eax, PD_HI_BASE | 0x03
    mov     dword [PDPT_HI_BASE + 510*8 + 0], eax
    mov     dword [PDPT_HI_BASE + 510*8 + 4], 0

    ; Map kernel: KERNEL_PMA -> KERNEL_VMA via 2MiB pages
    ; PD_HI[i] = (KERNEL_PMA + i*2MiB) | PS | RW | P
    mov     esi, KERNEL_2M_PAGES
    mov     ebx, KERNEL_PMA
    xor     edi, edi                     ; PD index

.map_loop:
    mov     eax, ebx
    or      eax, (1<<7) | 0x003
    mov     dword [PD_HI_BASE + edi*8 + 0], eax
    mov     dword [PD_HI_BASE + edi*8 + 4], 0
    add     ebx, 0x200000
    inc     edi
    dec     esi
    jnz     .map_loop

    pop     eax
    pop     ecx
    pop     edi
    ret

; ---------------------------
; GDT: 0x00 null, 0x08 64-bit code, 0x10 data
; ---------------------------
ALIGN 8
gdt:
    dq 0x0000000000000000             ; null
    dq 0x00AF9A000000FFFF             ; 0x08: 64-bit code (L-bit set in long mode; the exact flags are conventional)
    dq 0x00AF92000000FFFF             ; 0x10: data

gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt
gdt_end:

SECTION .boot.data
align 8
mb_magic:  dd 0
mb_info:   dd 0