#ifndef FB_CONSOLE_H
#define FB_CONSOLE_H

#include <cldtypes.h>

// Initialize framebuffer console from Multiboot2 framebuffer tag.
// Returns 1 if initialized and active, 0 otherwise (fallback to VGA).
int fb_console_init_from_mb2(u32 mb2_info);

// Query whether the framebuffer console is currently active.
int fb_console_is_active(void);
// True if framebuffer tag was found and mapped, even if font not ready yet.
int fb_console_present(void);

// Set text color using VGA-style 8-bit attribute (low 4 bits FG, high 4 bits BG).
void fb_console_set_color(u8 vga_attr);

// Grid operations for integration with vgaio
void fb_console_putc_at(char c, u8 vga_attr, int x, int y);
void fb_console_scroll_up(u8 vga_attr);
void fb_console_clear(void);
void fb_console_clear_line(int y, u8 vga_attr);
void fb_console_clear_to_eol(int x, int y, u8 vga_attr);
void fb_console_get_size(int* out_cols, int* out_rows);

// Optional: load PSF font from ramfs path (e.g., "/fonts/Lat15-Terminus16.psf").
// Requires cldramfs to be initialized and data loaded. Returns 1 on success.
int fb_console_load_psf_from_ramfs(const char* path);

#endif // FB_CONSOLE_H
