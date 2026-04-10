#ifndef GUI_PNG_H
#define GUI_PNG_H

#include <cldtypes.h>

typedef struct {
    u8 *rgba;
    u32 width;
    u32 height;
    u8 has_alpha;
} gui_png_t;

int gui_png_is_png(const void *data, u32 size);
int gui_png_load(const void *data, u32 size, gui_png_t *out);
const char *gui_png_last_error(void);
void gui_png_free(gui_png_t *png);
void gui_png_get_rgb(const gui_png_t *png, u32 x, u32 y, u8 rgb[3]);
void gui_png_get_rgba(const gui_png_t *png, u32 x, u32 y, u8 rgba[4]);
void gui_png_write_fb_pixel(u8 *dst, u8 fb_bpp, const u8 rgb[3]);
void gui_png_write_fb_pixel_rgba(u8 *dst, u8 fb_bpp, const u8 rgba[4], const u8 bg_rgb[3]);

#endif // GUI_PNG_H
