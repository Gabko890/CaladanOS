#include "wallpaper.h"
#include <fb/fb_console.h>
#include <cldramfs/cldramfs.h>
#include <kmalloc.h>
#include <string.h>

// Simple BMP loader with on-demand scaling to framebuffer size.
// Supports uncompressed 24bpp (BI_RGB) and 32bpp (BI_RGB/bitfields assumed BGRA/X).

// Framebuffer properties captured at load time
static u32 g_wp_w = 0;        // framebuffer width at preparation time
static u32 g_wp_h = 0;        // framebuffer height at preparation time
static u8  g_wp_bpp = 0;      // framebuffer bytes per pixel at preparation time

// Source BMP metadata (points into ramfs-backed content)
static const u8* g_img_base = 0; // pointer to BMP start ('B''M')
static u32 g_img_pix_off = 0;    // pixel array offset
static u32 g_img_w = 0;          // source width
static u32 g_img_h = 0;          // source height (absolute value)
static u16 g_img_bpp = 0;        // bits per pixel in source (24 or 32)
static u32 g_img_stride = 0;     // bytes per source row (padded to 4 bytes)
static int g_img_top_down = 0;   // 1 if top-down bitmap

// Temporary line buffer (one scanline in framebuffer format)
static u8* g_linebuf = 0;        // size: g_wp_w * g_wp_bpp

static int g_wp_ready = 0;

static inline u32 rd_le32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u16 rd_le16(const u8* p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}

// General row stride calculator: rows are padded to 4-byte boundaries.
static inline u32 bmp_stride(u32 w, u32 bits_per_pixel) {
    // pitch (bytes) = floor((w*bpp + 31) / 32) * 4
    u64 bits = (u64)w * (u64)bits_per_pixel;
    u64 stride = ((bits + 31) / 32) * 4;
    return (u32)stride;
}

int gui_wallpaper_load(const char* path) {
    g_wp_ready = 0;
    if (!path || !fb_console_present()) return 0;
    Node* f = cldramfs_resolve_path_file(path, 0);
    if (!f || !f->content || f->content_size < 54) return 0;
    const u8* p = (const u8*)f->content;

    // BMP header checks
    if (!(p[0] == 'B' && p[1] == 'M')) return 0;
    u32 pix_off = rd_le32(p + 10);
    u32 dib_size = rd_le32(p + 14);
    if (dib_size < 40) return 0; // need at least BITMAPINFOHEADER
    // width/height (height signed; negative => top-down)
    int32_t w_signed = (int32_t)rd_le32(p + 18);
    int32_t h_signed = (int32_t)rd_le32(p + 22);
    if (w_signed <= 0 || h_signed == 0) return 0;
    u32 w = (u32)w_signed;
    u32 h = (u32)(h_signed > 0 ? h_signed : -h_signed);
    int top_down = (h_signed < 0);

    u16 planes = rd_le16(p + 26);
    u16 bpp = rd_le16(p + 28);
    u32 compression = rd_le32(p + 30);
    if (planes != 1) return 0;
    if (!(bpp == 24 || bpp == 32)) return 0;
    if (!(compression == 0 || (bpp == 32 && compression == 3))) return 0; // accept BI_RGB; 32bpp may be bitfields

    // Sanity on sizes
    if (w == 0 || h == 0) return 0;
    if (pix_off >= f->content_size) return 0;

    // Capture framebuffer properties
    fb_get_resolution(&g_wp_w, &g_wp_h);
    g_wp_bpp = fb_get_bytespp();
    if (g_wp_w == 0 || g_wp_h == 0 || g_wp_bpp == 0) { g_wp_ready = 0; return 0; }

    // Record source image metadata (pointer remains valid in RAMFS)
    g_img_base = p;
    g_img_pix_off = pix_off;
    g_img_w = w;
    g_img_h = h;
    g_img_bpp = bpp;
    g_img_stride = bmp_stride(w, bpp);
    g_img_top_down = top_down;

    // Allocate/reallocate single-line buffer for blitting
    if (g_linebuf) { kfree(g_linebuf); g_linebuf = 0; }
    g_linebuf = (u8*)kmalloc((size_t)((u64)g_wp_w * (u64)g_wp_bpp));
    if (!g_linebuf) {
        // Unable to allocate even a single line; give up on wallpaper
        g_wp_ready = 0;
        return 0;
    }

    // Quick bounds safety: ensure we can read at least first/last pixel in-source
    if ((u64)g_img_pix_off + (u64)g_img_stride * (u64)(g_img_h - 1) + (u64)(g_img_w * (g_img_bpp / 8)) > (u64)f->content_size) {
        // Fallback to simple check without overflow prone math
        if ((u64)g_img_pix_off >= (u64)f->content_size) return 0;
    }

    g_wp_ready = 1;
    return 1;
}

int gui_wallpaper_is_loaded(void) {
    return g_wp_ready && g_img_base && g_linebuf && g_wp_w && g_wp_h && g_wp_bpp;
}

static inline const u8* src_row_ptr(u32 src_row) {
    return g_img_base + g_img_pix_off + (u64)src_row * (u64)g_img_stride;
}

static void build_scaled_row_into(u32 dst_y, u32 dst_x, u32 dst_w, u8* out_row) {
    // Map destination y to source y
    u32 sy = (u64)dst_y * (u64)g_img_h / (u64)g_wp_h;
    u32 src_row = g_img_top_down ? sy : (g_img_h - 1 - sy);
    const u8* row_ptr = src_row_ptr(src_row);
    u32 src_px_stride = (u32)(g_img_bpp / 8);

    // Build pixels for [dst_x, dst_x + dst_w)
    u8* d = out_row;
    for (u32 dx = 0; dx < dst_w; dx++) {
        u32 sx = (u64)(dst_x + dx) * (u64)g_img_w / (u64)g_wp_w;
        const u8* sp = row_ptr + (u64)sx * (u64)src_px_stride;
        u8 B = sp[0];
        u8 G = sp[1];
        u8 R = sp[2];
        if (g_wp_bpp == 4) {
            d[0] = B; d[1] = G; d[2] = R; d[3] = 0xFF;
        } else if (g_wp_bpp == 3) {
            d[0] = B; d[1] = G; d[2] = R;
        } else if (g_wp_bpp == 2) {
            u16 rv = ((u16)R & 0xF8) << 8;
            u16 gv = ((u16)G & 0xFC) << 3;
            u16 bv = ((u16)B & 0xF8) >> 3;
            u16 v = rv | gv | bv;
            d[0] = (u8)(v & 0xFF);
            d[1] = (u8)(v >> 8);
        }
        d += g_wp_bpp;
    }
}

void gui_wallpaper_draw_fullscreen(void) {
    if (!gui_wallpaper_is_loaded()) return;
    for (u32 y = 0; y < g_wp_h; y++) {
        build_scaled_row_into(y, 0, g_wp_w, g_linebuf);
        fb_blit(0, y, g_wp_w, 1, g_linebuf);
    }
}

void gui_wallpaper_redraw_rect(u32 x, u32 y, u32 w, u32 h) {
    if (!gui_wallpaper_is_loaded()) return;
    if (x >= g_wp_w || y >= g_wp_h) return;
    if (w == 0 || h == 0) return;
    if (x + w > g_wp_w) w = g_wp_w - x;
    if (y + h > g_wp_h) h = g_wp_h - y;
    for (u32 yy = 0; yy < h; yy++) {
        build_scaled_row_into(y + yy, x, w, g_linebuf);
        fb_blit(x, y + yy, w, 1, g_linebuf);
    }
}
