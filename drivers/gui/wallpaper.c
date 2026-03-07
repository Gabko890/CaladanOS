#include "wallpaper.h"
#include "bmp.h"
#include <fb/fb_console.h>
#include <cldramfs/cldramfs.h>
#include <kmalloc.h>
#include <string.h>

// Framebuffer properties captured at load time
static u32 g_wp_w = 0;        // framebuffer width at preparation time
static u32 g_wp_h = 0;        // framebuffer height at preparation time
static u8  g_wp_bpp = 0;      // framebuffer bytes per pixel at preparation time

static gui_bmp_t g_bmp;

// Temporary line buffer (one scanline in framebuffer format)
static u8* g_linebuf = 0;        // size: g_wp_w * g_wp_bpp

static int g_wp_ready = 0;

int gui_wallpaper_load(const char* path) {
    g_wp_ready = 0;
    memset(&g_bmp, 0, sizeof(g_bmp));
    if (!path || !fb_console_present()) return 0;
    Node* f = cldramfs_resolve_path_file(path, 0);
    if (!f || !f->content) return 0;
    if (!gui_bmp_parse(f->content, f->content_size, &g_bmp)) return 0;

    // Capture framebuffer properties
    fb_get_resolution(&g_wp_w, &g_wp_h);
    g_wp_bpp = fb_get_bytespp();
    if (g_wp_w == 0 || g_wp_h == 0 || g_wp_bpp == 0) { g_wp_ready = 0; return 0; }

    // Allocate/reallocate single-line buffer for blitting
    if (g_linebuf) { kfree(g_linebuf); g_linebuf = 0; }
    g_linebuf = (u8*)kmalloc((size_t)((u64)g_wp_w * (u64)g_wp_bpp));
    if (!g_linebuf) {
        // Unable to allocate even a single line; give up on wallpaper
        g_wp_ready = 0;
        return 0;
    }

    g_wp_ready = 1;
    return 1;
}

int gui_wallpaper_is_loaded(void) {
    return g_wp_ready && g_bmp.base && g_linebuf && g_wp_w && g_wp_h && g_wp_bpp;
}

static void build_scaled_row_into(u32 dst_y, u32 dst_x, u32 dst_w, u8* out_row) {
    u32 sy = (u64)dst_y * (u64)g_bmp.height / (u64)g_wp_h;
    u8* d = out_row;
    for (u32 dx = 0; dx < dst_w; dx++) {
        u32 sx = (u64)(dst_x + dx) * (u64)g_bmp.width / (u64)g_wp_w;
        u8 rgb[3];
        gui_bmp_get_rgb(&g_bmp, sx, sy, rgb);
        gui_bmp_write_fb_pixel(d, g_wp_bpp, rgb);
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
