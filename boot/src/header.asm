section .multiboot_header
header_start:
	; magic number
	dd 0xe85250d6 ; multiboot2
	; architecture
	dd 0 ; protected mode i386
	; header length
	dd header_end - header_start
	; checksum
	dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

	; framebuffer tag - request specific mode
	align 8
	dw 5         ; type = framebuffer
	dw 0         ; flags (not required)
	dd 20        ; size (8 + 3*4 = 20 bytes)
	dd 1024      ; width
	dd 768       ; height
	dd 32        ; depth (bits per pixel)

	; end tag
	align 8
	dw 0         ; type = end
	dw 0         ; flags
	dd 8         ; size
header_end:
