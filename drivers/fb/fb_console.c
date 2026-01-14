#include <cldtypes.h>
#include <string.h>

#include <multiboot/multiboot2.h>
#include <fb/fb_console.h>
#include <cldramfs/cldramfs.h>

// Minimal PSF v1 header
typedef struct {
    u8 magic0; // 0x36
    u8 magic1; // 0x04
    u8 mode;   // bit0: 512 glyphs
    u8 charsize; // bytes per glyph (height)
} __attribute__((packed)) psf1_header_t;

typedef struct {
    // Text cell size (in pixels)
    int cell_w; // always 8 for PSF1
    int cell_h; // charsize from header
    // Font glyphs
    const u8* glyphs;
    int glyph_count; // 256 or 512
    int glyph_size;  // bytes per glyph (equals cell_h)
} psf_font_t;

typedef struct {
    volatile u8* fb;    // framebuffer base (identity-mapped)
    u32 pitch;          // bytes per scanline
    u32 fb_width;       // pixels
    u32 fb_height;      // pixels
    u8  bpp;            // bits per pixel
    u8  bytes_pp;       // bytes per pixel
} fb_state_t;

static fb_state_t g_fb = {0};
static psf_font_t g_font = {0};
static int g_has_fb = 0;
static int g_has_font = 0;

static u8 g_color = 0x07; // VGA light grey on black

// VGA 16-color palette in RGB
static const u8 PALETTE[16][3] = {
    {0x00,0x00,0x00}, {0x00,0x00,0xAA}, {0x00,0xAA,0x00}, {0x00,0xAA,0xAA},
    {0xAA,0x00,0x00}, {0xAA,0x00,0xAA}, {0xAA,0x55,0x00}, {0xAA,0xAA,0xAA},
    {0x55,0x55,0x55}, {0x55,0x55,0xFF}, {0x55,0xFF,0x55}, {0x55,0xFF,0xFF},
    {0xFF,0x55,0x55}, {0xFF,0x55,0xFF}, {0xFF,0xFF,0x55}, {0xFF,0xFF,0xFF},
};

static inline u8 fg_idx(u8 v) { return v & 0x0F; }
static inline u8 bg_idx(u8 v) { return (v >> 4) & 0x0F; }

static void set_pixel(u32 x, u32 y, const u8 rgb[3]) {
    if (!g_has_fb) return;
    if (x >= g_fb.fb_width || y >= g_fb.fb_height) return;
    u32 off = y * g_fb.pitch + x * g_fb.bytes_pp;
    volatile u8* p = g_fb.fb + off;

    switch (g_fb.bytes_pp) {
        case 4:
            p[0] = rgb[2]; p[1] = rgb[1]; p[2] = rgb[0]; p[3] = 0xFF; // B,G,R,A
            break;
        case 3:
            p[0] = rgb[2]; p[1] = rgb[1]; p[2] = rgb[0];
            break;
        case 2: {
            // 5:6:5
            u16 r = ((u16)rgb[0] & 0xF8) << 8;
            u16 g = ((u16)rgb[1] & 0xFC) << 3;
            u16 b = ((u16)rgb[2] & 0xF8) >> 3;
            u16 v = r | g | b;
            p[0] = (u8)(v & 0xFF);
            p[1] = (u8)(v >> 8);
            break;
        }
        default:
            break;
    }
}

static void fill_rect(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    if (!g_has_fb) return;
    if (x >= g_fb.fb_width || y >= g_fb.fb_height) return;
    u32 x2 = x + w; if (x2 > g_fb.fb_width) x2 = g_fb.fb_width;
    u32 y2 = y + h; if (y2 > g_fb.fb_height) y2 = g_fb.fb_height;
    for (u32 yy = y; yy < y2; yy++) {
        for (u32 xx = x; xx < x2; xx++) {
            set_pixel(xx, yy, rgb);
        }
    }
}

static void draw_glyph(u32 cell_x, u32 cell_y, u8 ch, const u8 fg[3], const u8 bg[3]) {
    if (!g_has_font || !g_has_fb) return;
    const u32 px = cell_x * (u32)g_font.cell_w;
    const u32 py = cell_y * (u32)g_font.cell_h;

    u32 idx = (u32)ch;
    if (idx >= (u32)g_font.glyph_count) idx = (u32)'?';
    const u8* glyph = g_font.glyphs + idx * (u32)g_font.glyph_size;

    for (int y = 0; y < g_font.cell_h; y++) {
        if (py + (u32)y >= g_fb.fb_height) break;
        u8 bits = glyph[y];
        for (int x = 0; x < g_font.cell_w; x++) {
            if (px + (u32)x >= g_fb.fb_width) break;
            u8 mask = (u8)(0x80 >> (x & 7));
            const u8* rgb = (bits & mask) ? fg : bg;
            set_pixel(px + (u32)x, py + (u32)y, rgb);
        }
    }
}

static int cols(void) { return g_has_fb && g_has_font ? (int)(g_fb.fb_width / (u32)g_font.cell_w) : 0; }
static int rows(void) { return g_has_fb && g_has_font ? (int)(g_fb.fb_height / (u32)g_font.cell_h) : 0; }

static int load_psf_from_buffer(const void* buf, u32 len) {
    if (!buf || len < sizeof(psf1_header_t)) return 0;
    const psf1_header_t* hdr = (const psf1_header_t*)buf;
    if (!(hdr->magic0 == 0x36 && hdr->magic1 == 0x04)) return 0;
    if (sizeof(psf1_header_t) + hdr->charsize > len) return 0;
    g_font.cell_w = 8;
    g_font.cell_h = hdr->charsize;
    g_font.glyph_size = hdr->charsize;
    g_font.glyph_count = (hdr->mode & 0x01) ? 512 : 256;
    g_font.glyphs = (const u8*)buf + sizeof(psf1_header_t);
    g_has_font = 1;
    return 1;
}

// Public API
int fb_console_init_from_mb2(u32 mb2_info) {
    g_has_fb = 0;
    struct multiboot_tag* tag = multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_FRAMEBUFFER);
    if (!tag) return 0;

    const struct multiboot_tag_framebuffer* fb = (const struct multiboot_tag_framebuffer*)tag;

    g_fb.fb       = (volatile u8*)(uintptr_t)fb->framebuffer_addr;
    g_fb.pitch    = fb->framebuffer_pitch;
    g_fb.fb_width = fb->framebuffer_width;
    g_fb.fb_height= fb->framebuffer_height;
    g_fb.bpp      = fb->framebuffer_bpp;
    g_fb.bytes_pp = (u8)((fb->framebuffer_bpp + 7) / 8);
    g_has_fb = 1;
    return 1;
}

int fb_console_load_psf_from_ramfs(const char* path) {
    if (!g_has_fb || !path) return 0;
    Node* file = cldramfs_resolve_path_file(path, 0);
    if (!file || !file->content || file->content_size < 4) return 0;
    return load_psf_from_buffer(file->content, file->content_size);
}

int fb_console_is_active(void) {
    return g_has_fb && g_has_font;
}

int fb_console_present(void) {
    return g_has_fb;
}

void fb_console_set_color(u8 vga_attr) {
    g_color = vga_attr;
}

void fb_console_putc_at(char c, u8 vga_attr, int x, int y) {
    if (!fb_console_is_active()) return;
    if (x < 0 || y < 0) return;
    int cw = cols();
    int rh = rows();
    if (x >= cw || y >= rh) return;
    const u8* fg = PALETTE[fg_idx(vga_attr) & 0x0F];
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    draw_glyph((u32)x, (u32)y, (u8)c, fg, bg);
}

void fb_console_scroll_up(u8 vga_attr) {
    if (!fb_console_is_active()) return;
    const u32 row_px = (u32)g_font.cell_h;
    if (g_fb.fb_height <= row_px) {
        const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
        fill_rect(0, 0, g_fb.fb_width, g_fb.fb_height, bg);
        return;
    }
    // Scroll one text row
    for (u32 y = 0; y + row_px < g_fb.fb_height; y++) {
        volatile u8* dst = g_fb.fb + y * g_fb.pitch;
        volatile u8* src = g_fb.fb + (y + row_px) * g_fb.pitch;
        // Safe byte copy per scanline
        for (u32 i = 0; i < g_fb.pitch; i++) dst[i] = src[i];
    }
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    fill_rect(0, g_fb.fb_height - row_px, g_fb.fb_width, row_px, bg);
}

void fb_console_clear(void) {
    if (!fb_console_is_active()) return;
    const u8* bg = PALETTE[bg_idx(g_color) & 0x0F];
    fill_rect(0, 0, g_fb.fb_width, g_fb.fb_height, bg);
}

void fb_console_clear_line(int y, u8 vga_attr) {
    if (!fb_console_is_active()) return;
    if (y < 0 || y >= rows()) return;
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    u32 py = (u32)y * (u32)g_font.cell_h;
    fill_rect(0, py, g_fb.fb_width, (u32)g_font.cell_h, bg);
}

void fb_console_clear_to_eol(int x, int y, u8 vga_attr) {
    if (!fb_console_is_active()) return;
    if (x < 0 || y < 0) return;
    if (y >= rows()) return;
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    u32 px = (u32)x * (u32)g_font.cell_w;
    u32 py = (u32)y * (u32)g_font.cell_h;
    if (px >= g_fb.fb_width) return;
    fill_rect(px, py, g_fb.fb_width - px, (u32)g_font.cell_h, bg);
}

void fb_console_get_size(int* out_cols, int* out_rows) {
    if (out_cols) *out_cols = cols();
    if (out_rows) *out_rows = rows();
}

// ===== Exported simple RGB drawing helpers =====
void fb_get_resolution(u32* out_w, u32* out_h) {
    if (out_w) *out_w = g_has_fb ? g_fb.fb_width : 0;
    if (out_h) *out_h = g_has_fb ? g_fb.fb_height : 0;
}

void fb_draw_pixel(u32 x, u32 y, u8 r, u8 g, u8 b) {
    u8 rgb[3] = { r, g, b };
    set_pixel(x, y, rgb);
}

void fb_fill_rect_rgb(u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b) {
    u8 rgb[3] = { r, g, b };
    fill_rect(x, y, w, h, rgb);
}

u8 fb_get_bytespp(void) {
    return g_has_fb ? g_fb.bytes_pp : 0;
}

void fb_copy_out(u32 x, u32 y, u32 w, u32 h, u8* dst) {
    if (!g_has_fb || !dst) return;
    if (x >= g_fb.fb_width || y >= g_fb.fb_height) return;
    u32 x2 = x + w; if (x2 > g_fb.fb_width) x2 = g_fb.fb_width;
    u32 y2 = y + h; if (y2 > g_fb.fb_height) y2 = g_fb.fb_height;
    u32 out_w = x2 - x;
    u32 out_h = y2 - y;
    u32 bpp = g_fb.bytes_pp;
    for (u32 yy = 0; yy < out_h; yy++) {
        volatile u8* src = g_fb.fb + (y + yy) * g_fb.pitch + x * bpp;
        u8* d = dst + yy * (out_w * bpp);
        for (u32 i = 0; i < out_w * bpp; i++) d[i] = src[i];
    }
}

void fb_blit(u32 x, u32 y, u32 w, u32 h, const u8* src) {
    if (!g_has_fb || !src) return;
    if (x >= g_fb.fb_width || y >= g_fb.fb_height) return;
    u32 x2 = x + w; if (x2 > g_fb.fb_width) x2 = g_fb.fb_width;
    u32 y2 = y + h; if (y2 > g_fb.fb_height) y2 = g_fb.fb_height;
    u32 in_w = x2 - x;
    u32 in_h = y2 - y;
    u32 bpp = g_fb.bytes_pp;
    for (u32 yy = 0; yy < in_h; yy++) {
        volatile u8* dst = g_fb.fb + (y + yy) * g_fb.pitch + x * bpp;
        const u8* s = src + yy * (in_w * bpp);
        for (u32 i = 0; i < in_w * bpp; i++) dst[i] = s[i];
    }
}

int fb_font_get_cell_size(int* out_w, int* out_h) {
    if (!g_has_font) return 0;
    if (out_w) *out_w = g_font.cell_w;
    if (out_h) *out_h = g_font.cell_h;
    return 1;
}

void fb_fill_rect_attr(u32 x, u32 y, u32 w, u32 h, u8 vga_attr) {
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    fill_rect(x, y, w, h, bg);
}

void fb_draw_char_px(u32 px, u32 py, char c, u8 vga_attr) {
    if (!g_has_font || !g_has_fb) return;
    const u8* fg = PALETTE[fg_idx(vga_attr) & 0x0F];
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    // px,py are pixel coords; convert to cell coords for draw_glyph helper
    u32 cell_x = px / (u32)g_font.cell_w;
    u32 cell_y = py / (u32)g_font.cell_h;
    // draw_glyph expects cell grid aligned; draw directly using per-pixel to match px,py alignment
    // Implement local variant that draws starting at px,py in pixels
    u32 idx = (u32)(u8)c;
    if (idx >= (u32)g_font.glyph_count) idx = (u32)'?';
    const u8* glyph = g_font.glyphs + idx * (u32)g_font.glyph_size;
    for (int y2 = 0; y2 < g_font.cell_h; y2++) {
        if (py + (u32)y2 >= g_fb.fb_height) break;
        u8 bits = glyph[y2];
        for (int x2 = 0; x2 < g_font.cell_w; x2++) {
            if (px + (u32)x2 >= g_fb.fb_width) break;
            u8 mask = (u8)(0x80 >> (x2 & 7));
            const u8* rgb = (bits & mask) ? fg : bg;
            set_pixel(px + (u32)x2, py + (u32)y2, rgb);
        }
    }
}

void fb_draw_char_px_nobg(u32 px, u32 py, char c, u8 fg_index) {
    if (!g_has_font || !g_has_fb) return;
    const u8* fg = PALETTE[fg_index & 0x0F];
    u32 idx = (u32)(u8)c;
    if (idx >= (u32)g_font.glyph_count) idx = (u32)'?';
    const u8* glyph = g_font.glyphs + idx * (u32)g_font.glyph_size;
    for (int y2 = 0; y2 < g_font.cell_h; y2++) {
        if (py + (u32)y2 >= g_fb.fb_height) break;
        u8 bits = glyph[y2];
        for (int x2 = 0; x2 < g_font.cell_w; x2++) {
            if (px + (u32)x2 >= g_fb.fb_width) break;
            u8 mask = (u8)(0x80 >> (x2 & 7));
            if (bits & mask) {
                set_pixel(px + (u32)x2, py + (u32)y2, fg);
            }
        }
    }
}

void fb_scroll_region_up(u32 x, u32 y, u32 w, u32 h, u32 row_px, u8 vga_attr) {
    if (!g_has_fb) return;
    if (x >= g_fb.fb_width || y >= g_fb.fb_height) return;
    if (w == 0 || h == 0) return;
    if (row_px >= h) {
        fb_fill_rect_attr(x, y, w, h, vga_attr);
        return;
    }
    u32 bpp = g_fb.bytes_pp;
    u32 copy_h = h - row_px;
    for (u32 yy = 0; yy < copy_h; yy++) {
        volatile u8* dst = g_fb.fb + (y + yy) * g_fb.pitch + x * bpp;
        volatile u8* src = g_fb.fb + (y + yy + row_px) * g_fb.pitch + x * bpp;
        for (u32 i = 0; i < w * bpp; i++) dst[i] = src[i];
    }
    fb_fill_rect_attr(x, y + copy_h, w, row_px, vga_attr);
}

void fb_copy_region(u32 src_x, u32 src_y, u32 w, u32 h, u32 dst_x, u32 dst_y) {
    if (!g_has_fb) return;
    if (w == 0 || h == 0) return;
    if (src_x >= g_fb.fb_width || src_y >= g_fb.fb_height) return;
    if (dst_x >= g_fb.fb_width || dst_y >= g_fb.fb_height) return;
    u32 bpp = g_fb.bytes_pp;
    // Clamp to both source and dest bounds
    u32 max_w_src = g_fb.fb_width  - src_x;
    u32 max_w_dst = g_fb.fb_width  - dst_x;
    u32 max_h_src = g_fb.fb_height - src_y;
    u32 max_h_dst = g_fb.fb_height - dst_y;
    if (w > max_w_src) w = max_w_src;
    if (w > max_w_dst) w = max_w_dst;
    if (h > max_h_src) h = max_h_src;
    if (h > max_h_dst) h = max_h_dst;

    if (w == 0 || h == 0) return;

    // Determine safe copy order per row and per line
    int copy_bottom_up = (dst_y > src_y);
    int copy_right_to_left = 0;
    if (dst_y == src_y) {
        // Same row band; decide by x overlap
        if (dst_x > src_x) copy_right_to_left = 1;
    }

    if (copy_bottom_up) {
        for (u32 yyo = 0; yyo < h; yyo++) {
            u32 yy = h - 1 - yyo;
            volatile u8* s = g_fb.fb + (src_y + yy) * g_fb.pitch + src_x * bpp;
            volatile u8* d = g_fb.fb + (dst_y + yy) * g_fb.pitch + dst_x * bpp;
            if (d == s) continue;
            if (copy_right_to_left) {
                for (u32 i = w * bpp; i > 0; i--) d[i-1] = s[i-1];
            } else {
                for (u32 i = 0; i < w * bpp; i++) d[i] = s[i];
            }
        }
    } else {
        for (u32 yy = 0; yy < h; yy++) {
            volatile u8* s = g_fb.fb + (src_y + yy) * g_fb.pitch + src_x * bpp;
            volatile u8* d = g_fb.fb + (dst_y + yy) * g_fb.pitch + dst_x * bpp;
            if (d == s) continue;
            if (copy_right_to_left) {
                for (u32 i = w * bpp; i > 0; i--) d[i-1] = s[i-1];
            } else {
                for (u32 i = 0; i < w * bpp; i++) d[i] = s[i];
            }
        }
    }
}
