; boot32_safe_tables.asm — safer: place page-tables at TABLES_PMA (0x00400000) to avoid overlap,
; emit debug bytes to ports 0xE9/0x80 to trace progress, identity-map first 16GiB with 2MiB pages,
; also keep higher-half mapping and VGA override.
BITS 32
default rel

%define KERNEL_PMA         0x00200000
%define KERNEL_VMA         0xFFFFFFFF80000000

%define TABLES_PMA         0x00400000    ; use this area for all page-tables (safe, high enough)
%define TABLES_VMA         0xFFFFFFFF80400000

; We'll place structures starting at TABLES_PMA:
%define PML4_BASE          (TABLES_PMA + 0x0000)   ; 0x00400000
%define PDPT_LO_BASE       (TABLES_PMA + 0x1000)   ; 0x00401000
%define PD_LO_BASE         (TABLES_PMA + 0x2000)   ; start of 16 PDs (each 0x1000): 0x00402000 .. 0x0041F000
%define PDPT_HI_BASE       (TABLES_PMA + 0x22000)  ; after low PDs (choose safe gap)
%define PD_HI_BASE         (TABLES_PMA + 0x23000)
%define PT_HI_BASE         (TABLES_PMA + 0x24000)

%define CR0_PG             (1 << 31)
%define CR4_PAE            (1 << 5)
%define EFER_MSR           0xC0000080
%define EFER_LME           (1 << 8)

%define KERNEL_2M_PAGES    2
%define TABLES_2M_PAGES    1

SECTION .boot.text align=16
global _start
extern long_mode_entry

global mb_info
global mb_magic

; optionally later:
; extern handle_error_multiboot
; extern handle_error_cpuid
; extern handle_error_longmode

_start:
    cli

    mov [mb_magic], eax
    mov [mb_info], ebx

    ; load GDT
    lgdt    [gdt_ptr]
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; ----------------------------
    ; EARLY ENVIRONMENT CHECKS
    ; ----------------------------
    call    check_multiboot
    call    check_cpuid
    call    check_long_mode

    ; (no VGA writes — we rely on framebuffer later)

    ; build page tables (in memory at TABLES_PMA)
    call    build_paging

    ;

    ; enable PAE before loading CR3
    mov     eax, cr4
    or      eax, CR4_PAE
    mov     cr4, eax

    ; load CR3 with physical address of PML4 (must be physical)
    mov     eax, PML4_BASE
    mov     cr3, eax

    ; enable Long Mode in EFER MSR
    mov     ecx, EFER_MSR
    rdmsr
    or      eax, EFER_LME
    wrmsr

    ;

    ; enable paging (CR0.PG)
    mov     eax, cr0
    or      eax, CR0_PG
    mov     cr0, eax

    ; (no VGA writes in paging phase)

    ; Emit final debug to ports so remote debugbox can see it
    mov     al, 'X'
    out     0xE9, al
    out     0x80, al

    ; Far-jump to 64-bit entry
    jmp     0x08:long_mode_entry

; ------------------------------------------------------------------------
; CHECKS
; ------------------------------------------------------------------------
check_multiboot:
    ; Check Multiboot magic (passed in EAX at entry)
    cmp dword [mb_magic], 0x36d76289
    jne .fail
    ret
.fail:
    mov al, 'M'
    ; also emit to debug port so QEMU/Bochs sees it even if VGA is not usable
    out 0xE9, al
    out 0x80, al
    jmp error

check_cpuid:
    ; Detect if CPUID instruction is supported (toggle ID bit in EFLAGS)
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1<<21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .fail
    ret
.fail:
    mov al, 'C'
    out 0xE9, al
    out 0x80, al
    jmp error

check_long_mode:
    ; Check for extended CPUID range and long mode bit in EDX of 0x80000001
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .fail
    mov eax, 0x80000001
    cpuid
    test edx, 1<<29
    jz .fail
    ret
.fail:
    mov al, 'L'
    out 0xE9, al
    out 0x80, al
    jmp error

; ------------------------------------------------------------------------
; ERROR HANDLER
; ------------------------------------------------------------------------
error:
    ; emit error code to debug ports for visibility (no VGA writes)
    out 0xE9, al
    out 0x80, al
    hlt

    ; later replace with calls to C:
    ; ; call handle_error_multiboot  ; (comment placeholder)
    ; ; call handle_error_cpuid
    ; ; call handle_error_longmode

; ------------------------------------------------------------------------
; build_paging - create PML4, PDPT_LO, 16 PDs to map 0..16GiB with 2MiB pages,
; and build high mapping used by kernel: PML4[511] -> PDPT_HI -> PD_HI -> PT_HI
; Emitting debug bytes to help trace progress.
; ------------------------------------------------------------------------
build_paging:
    push    edi
    push    esi
    push    ecx
    push    ebx
    push    edx
    push    ebp
    push    eax

    ; Debug: indicate entry to build_paging (port 0xE9)
    mov     al, '1'
    out     0xE9, al
    out     0x80, al

    xor     eax, eax
    mov     edi, PML4_BASE

    ; zero out the table area: pick safe size: 0x30000 bytes (192 KiB) -> (0x30000/4) dwords
    mov     ecx, (0x30000/4)
    rep stosd

    ; Debug: cleared table area
    mov     al, '2'
    out     0xE9, al
    out     0x80, al

    ; PML4[0] -> PDPT_LO
    mov     eax, PDPT_LO_BASE | 0x03
    mov     dword [PML4_BASE + 0*8 + 0], eax
    mov     dword [PML4_BASE + 0*8 + 4], 0

    ; PML4[511] -> PDPT_HI
    mov     eax, PDPT_HI_BASE | 0x03
    mov     dword [PML4_BASE + 511*8 + 0], eax
    mov     dword [PML4_BASE + 511*8 + 4], 0

    ; Debug: PML4 entries written
    mov     al, '3'
    out     0xE9, al
    out     0x80, al

    ; PDPT_LO[0..15] -> PD_LO_BASE + i*0x1000 (create 16 PDs)
    xor     esi, esi            ; i = 0
.fill_pdpt_lo:
    mov     eax, PD_LO_BASE
    mov     ebx, esi
    shl     ebx, 12            ; ebx = i * 0x1000
    add     eax, ebx
    or      eax, 0x03
    mov     dword [PDPT_LO_BASE + esi*8 + 0], eax
    mov     dword [PDPT_LO_BASE + esi*8 + 4], 0
    inc     esi
    cmp     esi, 16
    jl      .fill_pdpt_lo

    ; Debug: PDPT_LO entries done
    mov     al, '4'
    out     0xE9, al
    out     0x80, al

    ; Fill each PD (16 PDs) with 512 entries mapping 2MiB pages for identity mapping
    xor     esi, esi            ; pd index i = 0
.fill_pds_loop:
    ; edi = PD_LO_BASE + i*0x1000
    mov     edi, PD_LO_BASE
    mov     ebx, esi
    shl     ebx, 12
    add     edi, ebx           ; edi points to PD table memory

    ; physical base for this PD = i * 1GiB (i << 30)
    mov     ebx, esi
    shl     ebx, 30            ; low 32 bits
    mov     edx, esi
    shr     edx, 2             ; high 32 bits = i >> 2

    mov     ecx, 512
    xor     ebp, ebp           ; offset within PD
.inner_pd_fill:
    mov     eax, ebx
    or      eax, (1<<7) | 0x003
    mov     dword [edi + ebp + 0], eax
    mov     dword [edi + ebp + 4], edx

    add     ebx, 0x00200000
    adc     edx, 0

    add     ebp, 8
    dec     ecx
    jnz     .inner_pd_fill

    inc     esi
    cmp     esi, 16
    jl      .fill_pds_loop

    ; Debug: low PDs filled
    mov     al, '5'
    out     0xE9, al
    out     0x80, al

    ; ----------------------------------------------------------------
    ; Setup higher-half mapping
    ; PDPT_HI[510] -> PD_HI
    mov     eax, PD_HI_BASE | 0x03
    mov     dword [PDPT_HI_BASE + 510*8 + 0], eax
    mov     dword [PDPT_HI_BASE + 510*8 + 4], 0

    ; Create PT for PD_HI[0]
    mov     eax, PT_HI_BASE | 0x03
    mov     dword [PD_HI_BASE + 0*8 + 0], eax
    mov     dword [PD_HI_BASE + 0*8 + 4], 0

    ; Debug: high PDPT/PD set
    mov     al, '6'
    out     0xE9, al
    out     0x80, al

    ; Fill PT_HI_BASE entries: each entry maps KERNEL_PMA + idx*4K
    mov     esi, 0
    mov     ecx, 512
    mov     ebx, KERNEL_PMA
.fill_pt_loop:
    mov     eax, ebx
    or      eax, 0x003
    mov     dword [PT_HI_BASE + esi + 0], eax
    mov     dword [PT_HI_BASE + esi + 4], 0
    add     ebx, 0x1000
    add     esi, 8
    dec     ecx
    jnz     .fill_pt_loop

    ; PT_HI filled
    mov     al, '7'
    out     0xE9, al
    out     0x80, al

    ; Map remaining kernel 2MiB pages (if any)
    mov     esi, 1
    mov     ecx, KERNEL_2M_PAGES
    dec     ecx
    jz      .skip_kernel_loop
    mov     ebx, KERNEL_PMA + 0x200000
.kernel_loop:
    mov     eax, ebx
    or      eax, (1<<7) | 0x003
    mov     dword [PD_HI_BASE + esi*8 + 0], eax
    mov     dword [PD_HI_BASE + esi*8 + 4], 0
    add     ebx, 0x200000
    inc     esi
    dec     ecx
    jnz     .kernel_loop
.skip_kernel_loop:

    ; Map tables region after kernel entries (for TABLES_PMA if desired)
    mov     ecx, TABLES_2M_PAGES
    mov     ebx, TABLES_PMA
.map_tables_loop:
    mov     eax, ebx
    or      eax, (1<<7) | 0x003
    mov     dword [PD_HI_BASE + esi*8 + 0], eax
    mov     dword [PD_HI_BASE + esi*8 + 4], 0
    add     ebx, 0x200000
    inc     esi
    dec     ecx
    jnz     .map_tables_loop

    ; Also place TABLES_PMA into low PD_LO[0] at PD index 2 for convenience (identity)
    mov     eax, (TABLES_PMA) | (1<<7) | 0x003
    mov     dword [PD_LO_BASE + 2*8 + 0], eax
    mov     dword [PD_LO_BASE + 2*8 + 4], 0

    ; Debug: finished build_paging
    mov     al, '9'
    out     0xE9, al
    out     0x80, al

    pop     eax
    pop     ebp
    pop     edx
    pop     ebx
    pop     ecx
    pop     esi
    pop     edi
    ret

ALIGN 8
gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF

gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt
gdt_end:

SECTION .boot.data
align 8
mb_magic:  dd 0
mb_info:   dd 0
