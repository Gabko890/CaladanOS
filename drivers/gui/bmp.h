#ifndef GUI_BMP_H
#define GUI_BMP_H

#include <cldtypes.h>

typedef struct {
    const u8 *base;
    u32 size;
    u32 pix_off;
    u32 width;
    u32 height;
    u32 stride;
    u16 bpp;
    int top_down;
    int indexed;
    const u8 *palette;
    u32 palette_count;
    u32 palette_entry_size;
    u32 red_mask;
    u32 green_mask;
    u32 blue_mask;
} gui_bmp_t;

int gui_bmp_parse(const void *data, u32 size, gui_bmp_t *out);
void gui_bmp_get_rgb(const gui_bmp_t *bmp, u32 x, u32 y, u8 rgb[3]);
void gui_bmp_write_fb_pixel(u8 *dst, u8 fb_bpp, const u8 rgb[3]);

#endif // GUI_BMP_H
