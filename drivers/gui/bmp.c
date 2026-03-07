#include "bmp.h"
#include <string.h>

#define BI_RGB       0
#define BI_RLE8      1
#define BI_RLE4      2
#define BI_BITFIELDS 3
#define BI_ALPHABITFIELDS 6

static inline u16 rd_le16(const u8 *p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}

static inline u32 rd_le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u32 bmp_stride(u32 w, u32 bits_per_pixel) {
    u64 bits = (u64)w * (u64)bits_per_pixel;
    return (u32)(((bits + 31) / 32) * 4);
}

static int mask_shift(u32 mask) {
    int shift = 0;
    if (!mask) return 0;
    while ((mask & 1) == 0) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static int mask_bits(u32 mask) {
    int bits = 0;
    mask >>= mask_shift(mask);
    while (mask & 1) {
        bits++;
        mask >>= 1;
    }
    return bits;
}

static u8 scale_masked(u32 pixel, u32 mask) {
    if (!mask) return 0;
    int shift = mask_shift(mask);
    int bits = mask_bits(mask);
    u32 value = (pixel & mask) >> shift;
    if (bits <= 0) return 0;
    u32 max = (1u << bits) - 1u;
    return (u8)((value * 255u + max / 2u) / max);
}

static u8 palette_component(const gui_bmp_t *bmp, u32 index, int component) {
    if (!bmp->palette || index >= bmp->palette_count) return 0;
    const u8 *p = bmp->palette + index * bmp->palette_entry_size;
    if (bmp->palette_entry_size == 3) {
        if (component == 0) return p[2];
        if (component == 1) return p[1];
        return p[0];
    }
    if (component == 0) return p[2];
    if (component == 1) return p[1];
    return p[0];
}

static u32 read_index(const u8 *row, u32 x, u16 bpp) {
    if (bpp == 8) return row[x];
    if (bpp == 4) {
        u8 v = row[x / 2];
        return (x & 1) ? (v & 0x0F) : (v >> 4);
    }
    if (bpp == 1) {
        u8 v = row[x / 8];
        return (v >> (7 - (x & 7))) & 1;
    }
    return 0;
}

int gui_bmp_parse(const void *data, u32 size, gui_bmp_t *out) {
    if (!data || !out || size < 26) return 0;
    const u8 *p = (const u8*)data;
    if (p[0] != 'B' || p[1] != 'M') return 0;

    memset(out, 0, sizeof(*out));
    out->base = p;
    out->size = size;
    out->pix_off = rd_le32(p + 10);
    if (out->pix_off >= size) return 0;

    u32 dib_size = rd_le32(p + 14);
    u32 width = 0, height = 0, colors_used = 0, compression = BI_RGB;
    u16 planes = 0, bpp = 0;
    int top_down = 0;
    u32 palette_start = 14 + dib_size;
    u32 palette_entry_size = 4;

    if (dib_size == 12) {
        if (size < 26) return 0;
        width = rd_le16(p + 18);
        height = rd_le16(p + 20);
        planes = rd_le16(p + 22);
        bpp = rd_le16(p + 24);
        palette_entry_size = 3;
    } else if (dib_size >= 40) {
        if (size < 54 || 14 + dib_size > size) return 0;
        int32_t w_signed = (int32_t)rd_le32(p + 18);
        int32_t h_signed = (int32_t)rd_le32(p + 22);
        if (w_signed <= 0 || h_signed == 0) return 0;
        width = (u32)w_signed;
        height = (u32)(h_signed > 0 ? h_signed : -h_signed);
        top_down = h_signed < 0;
        planes = rd_le16(p + 26);
        bpp = rd_le16(p + 28);
        compression = rd_le32(p + 30);
        colors_used = rd_le32(p + 46);
    } else {
        return 0;
    }

    if (planes != 1 || width == 0 || height == 0) return 0;
    if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) return 0;
    if (compression == BI_RLE8 || compression == BI_RLE4) return 0;
    if (!(compression == BI_RGB || compression == BI_BITFIELDS || compression == BI_ALPHABITFIELDS)) return 0;
    if ((compression == BI_BITFIELDS || compression == BI_ALPHABITFIELDS) && !(bpp == 16 || bpp == 32)) return 0;

    out->width = width;
    out->height = height;
    out->bpp = bpp;
    out->top_down = top_down;
    out->stride = bmp_stride(width, bpp);
    if ((u64)out->pix_off + (u64)out->stride * (u64)(height - 1) + ((u64)width * (u64)bpp + 7u) / 8u > (u64)size) return 0;

    if (bpp <= 8) {
        u32 max_colors = 1u << bpp;
        u32 count = colors_used ? colors_used : max_colors;
        if (count > max_colors) count = max_colors;
        if (palette_start >= out->pix_off) return 0;
        u32 available = (out->pix_off - palette_start) / palette_entry_size;
        if (count > available) count = available;
        if (count == 0) return 0;
        out->indexed = 1;
        out->palette = p + palette_start;
        out->palette_count = count;
        out->palette_entry_size = palette_entry_size;
        return 1;
    }

    if (compression == BI_RGB) {
        if (bpp == 16) {
            out->red_mask = 0x7C00;
            out->green_mask = 0x03E0;
            out->blue_mask = 0x001F;
        } else if (bpp == 32) {
            out->red_mask = 0x00FF0000;
            out->green_mask = 0x0000FF00;
            out->blue_mask = 0x000000FF;
        }
    } else {
        u32 mask_off = 14 + 40;
        if (mask_off + 12 > size || mask_off + 12 > out->pix_off) return 0;
        out->red_mask = rd_le32(p + mask_off);
        out->green_mask = rd_le32(p + mask_off + 4);
        out->blue_mask = rd_le32(p + mask_off + 8);
    }

    return 1;
}

void gui_bmp_get_rgb(const gui_bmp_t *bmp, u32 x, u32 y, u8 rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (!bmp || !bmp->base || x >= bmp->width || y >= bmp->height) return;
    u32 src_y = bmp->top_down ? y : (bmp->height - 1 - y);
    const u8 *row = bmp->base + bmp->pix_off + (u64)src_y * (u64)bmp->stride;

    if (bmp->indexed) {
        u32 index = read_index(row, x, bmp->bpp);
        rgb[0] = palette_component(bmp, index, 0);
        rgb[1] = palette_component(bmp, index, 1);
        rgb[2] = palette_component(bmp, index, 2);
        return;
    }

    if (bmp->bpp == 24) {
        const u8 *sp = row + (u64)x * 3u;
        rgb[0] = sp[2];
        rgb[1] = sp[1];
        rgb[2] = sp[0];
    } else if (bmp->bpp == 16) {
        u32 pixel = rd_le16(row + (u64)x * 2u);
        rgb[0] = scale_masked(pixel, bmp->red_mask);
        rgb[1] = scale_masked(pixel, bmp->green_mask);
        rgb[2] = scale_masked(pixel, bmp->blue_mask);
    } else if (bmp->bpp == 32) {
        u32 pixel = rd_le32(row + (u64)x * 4u);
        rgb[0] = scale_masked(pixel, bmp->red_mask);
        rgb[1] = scale_masked(pixel, bmp->green_mask);
        rgb[2] = scale_masked(pixel, bmp->blue_mask);
    }
}

void gui_bmp_write_fb_pixel(u8 *dst, u8 fb_bpp, const u8 rgb[3]) {
    if (!dst || !rgb) return;
    if (fb_bpp == 4) {
        dst[0] = rgb[2];
        dst[1] = rgb[1];
        dst[2] = rgb[0];
        dst[3] = 0xFF;
    } else if (fb_bpp == 3) {
        dst[0] = rgb[2];
        dst[1] = rgb[1];
        dst[2] = rgb[0];
    } else if (fb_bpp == 2) {
        u16 r = ((u16)rgb[0] & 0xF8) << 8;
        u16 g = ((u16)rgb[1] & 0xFC) << 3;
        u16 b = ((u16)rgb[2] & 0xF8) >> 3;
        u16 v = r | g | b;
        dst[0] = (u8)(v & 0xFF);
        dst[1] = (u8)(v >> 8);
    }
}
