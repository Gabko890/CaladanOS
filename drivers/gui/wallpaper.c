#include "wallpaper.h"
#include <fb/fb_console.h>
#include <cldramfs/cldramfs.h>
#include <kmalloc.h>
#include <string.h>

// Simple BMP loader and scaler to framebuffer size.
// Supports uncompressed 24bpp (BI_RGB) and 32bpp (BI_RGB/bitfields assumed BGRA/X).

static u8* g_wp_scaled = 0;   // scaled image buffer matching framebuffer (w*h*fb_bpp)
static u32 g_wp_w = 0;        // framebuffer width at preparation time
static u32 g_wp_h = 0;        // framebuffer height at preparation time
static u8  g_wp_bpp = 0;      // framebuffer bytes per pixel at preparation time
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
    // width/height
    // Dimensions are signed for height (top-down if negative)
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

    // Prepare scaled buffer (will read directly from content)
    // Note: Our scaler assumes bottom-up if height positive.
    // If top-down, adjust flag in convert function by flipping bottom_up.
    // For now, pass bottom_up= !top_down via local flag by re-computing row pointer.

    // We reuse the convert function with bottom-up assumed; emulate top-down by setting h and flipping logic.
    // Implement top-down by passing bottom_up=0 if needed (local override inside function not exposed).
    // A quick local wrapper:
    // We duplicate minimal code to handle top-down case efficiently.

    fb_get_resolution(&g_wp_w, &g_wp_h);
    g_wp_bpp = fb_get_bytespp();
    if (g_wp_w == 0 || g_wp_h == 0 || g_wp_bpp == 0) { g_wp_ready = 0; return 0; }

    // Allocate scaled buffer
    u64 needed = (u64)g_wp_w * (u64)g_wp_h * (u64)g_wp_bpp;
    if (g_wp_scaled) { kfree(g_wp_scaled); g_wp_scaled = 0; }
    g_wp_scaled = (u8*)kmalloc((size_t)needed);
    if (!g_wp_scaled) { g_wp_ready = 0; return 0; }

    u32 src_stride = bmp_stride(w, bpp);

    // Safety: ensure at least first and last row are within buffer
    if ((u64)pix_off + (u64)src_stride * (u64)(h - 1) + (u64)(w * (bpp / 8)) > (u64)f->content_size) {
        // Fallback to simple check without overflow prone math
        if ((u64)pix_off >= (u64)f->content_size) return 0;
    }

    for (u32 dy = 0; dy < g_wp_h; dy++) {
        u32 sy = (u64)dy * (u64)h / (u64)g_wp_h;
        u32 src_row = top_down ? sy : (h - 1 - sy);
        const u8* row_ptr = p + pix_off + src_row * src_stride;
        u8* dst = g_wp_scaled + (u64)dy * (u64)g_wp_w * (u64)g_wp_bpp;
        for (u32 dx = 0; dx < g_wp_w; dx++) {
            u32 sx = (u64)dx * (u64)w / (u64)g_wp_w;
            const u8* sp = row_ptr + (u64)sx * (u64)(bpp / 8);
            u8 B = sp[0];
            u8 G = sp[1];
            u8 R = sp[2];
            if (g_wp_bpp == 4) {
                dst[0] = B; dst[1] = G; dst[2] = R; dst[3] = 0xFF;
            } else if (g_wp_bpp == 3) {
                dst[0] = B; dst[1] = G; dst[2] = R;
            } else if (g_wp_bpp == 2) {
                u16 rv = ((u16)R & 0xF8) << 8;
                u16 gv = ((u16)G & 0xFC) << 3;
                u16 bv = ((u16)B & 0xF8) >> 3;
                u16 v = rv | gv | bv;
                dst[0] = (u8)(v & 0xFF);
                dst[1] = (u8)(v >> 8);
            }
            dst += g_wp_bpp;
        }
    }

    g_wp_ready = 1;
    return 1;
}

int gui_wallpaper_is_loaded(void) {
    return g_wp_ready && g_wp_scaled && g_wp_w && g_wp_h && g_wp_bpp;
}

void gui_wallpaper_draw_fullscreen(void) {
    if (!gui_wallpaper_is_loaded()) return;
    fb_blit(0, 0, g_wp_w, g_wp_h, g_wp_scaled);
}

void gui_wallpaper_redraw_rect(u32 x, u32 y, u32 w, u32 h) {
    if (!gui_wallpaper_is_loaded()) return;
    if (x >= g_wp_w || y >= g_wp_h) return;
    if (w == 0 || h == 0) return;
    // Clamp to prepared size
    if (x + w > g_wp_w) w = g_wp_w - x;
    if (y + h > g_wp_h) h = g_wp_h - y;
    // Blit row-by-row because fb_blit expects tightly packed rows
    for (u32 yy = 0; yy < h; yy++) {
        const u8* src_line = g_wp_scaled + (u64)(y + yy) * (u64)g_wp_w * (u64)g_wp_bpp + (u64)x * (u64)g_wp_bpp;
        fb_blit(x, y + yy, w, 1, src_line);
    }
}
