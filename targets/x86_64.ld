/* linker.ld
 *
 * Layout:
 *  - .boot.* sections: low, identity mapped (start at 1 MiB)
 *  - main kernel: higher half (0xFFFFFFFF80000000), loaded physically at 2 MiB
 *
 * Entry symbol is _start (from boot32.asm)
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Where the boot stubs live */
BOOT_VMA   = 0x00100000;          /* 1 MiB */
BOOT_LMA   = 0x00100000;

/* Higher-half kernel */
KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x00200000;          /* 2 MiB physical load address */

SECTIONS
{
  /* ---------------- Boot stub (identity mapped) ---------------- */
  . = BOOT_VMA;

  .boot.text : AT(BOOT_LMA) {
    KEEP(*(.boot.text*))
  }

  .boot.data : AT(BOOT_LMA + (ADDR(.boot.data) - BOOT_VMA)) {
    KEEP(*(.boot.data*))
  }

  .boot.rodata : AT(BOOT_LMA + (ADDR(.boot.rodata) - BOOT_VMA)) {
    KEEP(*(.boot.rodata*))
  }

  .boot.bss (NOLOAD) : {
    KEEP(*(.boot.bss*))
  }

  /* ---------------- Main kernel in higher half ---------------- */
  . = KERNEL_VMA;

  .text : AT(KERNEL_LMA + (ADDR(.text) - KERNEL_VMA)) {
    *(.text .text.*)
  }

  .rodata : AT(KERNEL_LMA + (ADDR(.rodata) - KERNEL_VMA)) {
    *(.rodata .rodata.*)
  }

  .data : AT(KERNEL_LMA + (ADDR(.data) - KERNEL_VMA)) {
    *(.data .data.*)
  }

  .bss (NOLOAD) : {
    *(COMMON)
    *(.bss .bss.*)
    . = ALIGN(16);
    __stack_top_hh = .;             /* symbol for top of higher-half stack */
    . += 0x40000;                   /* 256 KiB stack */
    __stack_bottom_hh = .;
  }

  _end = .; /* end of the kernel */
}
