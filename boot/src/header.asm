section .multiboot_header
align 8
header_start:
	; magic number
	dd 0xe85250d6 ; multiboot2
	; architecture
	dd 0 ; protected mode i386
	; header length
	dd header_end - header_start
	; checksum
	dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

	; request a framebuffer from the bootloader (Multiboot2 header tag #5)
	dw 5          ; type = MULTIBOOT_HEADER_TAG_FRAMEBUFFER
	dw 0          ; flags = 0 (optional)
	dd 20         ; size of this tag (bytes, including header)
	dd 1920       ; width
	dd 1080       ; height
	dd 32         ; depth (bpp)
	; pad to 8-byte alignment for next tag
	dd 0

	; end tag
	dw 0
	dw 0
	dd 8
header_end:
