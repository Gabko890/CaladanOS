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

// Minimal RGB drawing helpers for simple GUI usage
void fb_get_resolution(u32* out_w, u32* out_h);
void fb_draw_pixel(u32 x, u32 y, u8 r, u8 g, u8 b);
void fb_fill_rect_rgb(u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b);

// Raw framebuffer helpers (tightly packed rows in buffers)
u8  fb_get_bytespp(void);
void fb_copy_out(u32 x, u32 y, u32 w, u32 h, u8* dst);
void fb_blit(u32 x, u32 y, u32 w, u32 h, const u8* src);

// Text/glyph helpers for windowed terminals
int  fb_font_get_cell_size(int* out_w, int* out_h);
void fb_draw_char_px(u32 px, u32 py, char c, u8 vga_attr);
// Draw char using only foreground; background remains untouched. fg_index is 0-15 VGA color index.
void fb_draw_char_px_nobg(u32 px, u32 py, char c, u8 fg_index);
void fb_fill_rect_attr(u32 x, u32 y, u32 w, u32 h, u8 vga_attr);
// Fill rect using the foreground color from a VGA attribute (does not touch background).
void fb_fill_rect_fg(u32 x, u32 y, u32 w, u32 h, u8 vga_attr);
void fb_scroll_region_up(u32 x, u32 y, u32 w, u32 h, u32 row_px, u8 vga_attr);
// Copy rectangle within framebuffer (safe for overlap). Uses a small line buffer.
void fb_copy_region(u32 src_x, u32 src_y, u32 w, u32 h, u32 dst_x, u32 dst_y);

#endif // FB_CONSOLE_H
