ENTRY(start)

SECTIONS
{
    . = 1M;

    .boot :
    {
        KEEP(*(.multiboot_header))
    }

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss COMMON)
        *(COMMON)
        *(COMMON *)
        . = ALIGN(0x1000); /* page align */
    }

    _end = .; /* end of the kernel */
}

