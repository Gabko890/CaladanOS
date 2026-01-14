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
