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
    int cell_w;
    int cell_h;
    // Font glyphs
    const u8* glyphs;
    int glyph_count;
    int glyph_size;
    int bytes_per_row;
} psf_font_t;

typedef struct {
    u32 magic;       // 0x864ab572
    u32 version;
    u32 headersize;
    u32 flags;       // bit0: unicode table follows glyphs
    u32 length;      // glyph count
    u32 charsize;    // bytes per glyph
    u32 height;
    u32 width;
} __attribute__((packed)) psf2_header_t;

typedef struct {
    volatile u8* fb;    // framebuffer base (identity-mapped)
    u32 pitch;          // bytes per scanline
    u32 fb_width;       // pixels
    u32 fb_height;      // pixels
    u8  bpp;            // bits per pixel
    u8  bytes_pp;       // bytes per pixel
} fb_state_t;

static fb_state_t g_fb = {0};
static psf_font_t g_console_font = {0};
static psf_font_t g_gui_font = {0};
static int g_has_fb = 0;
static int g_has_console_font = 0;
static int g_has_gui_font = 0;

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

static int glyph_pixel_is_set(const psf_font_t* font, const u8* glyph, int x, int y) {
    if (!font || !glyph) return 0;
    const u8* row = glyph + (u32)y * (u32)font->bytes_per_row;
    u8 bits = row[x / 8];
    u8 mask = (u8)(0x80 >> (x & 7));
    return (bits & mask) ? 1 : 0;
}

static void draw_glyph(const psf_font_t* font, u32 cell_x, u32 cell_y, u8 ch, const u8 fg[3], const u8 bg[3]) {
    if (!font || !g_has_fb) return;
    const u32 px = cell_x * (u32)font->cell_w;
    const u32 py = cell_y * (u32)font->cell_h;

    u32 idx = (u32)ch;
    if (idx >= (u32)font->glyph_count) idx = (u32)'?';
    if (idx >= (u32)font->glyph_count) idx = 0;
    const u8* glyph = font->glyphs + idx * (u32)font->glyph_size;

    for (int y = 0; y < font->cell_h; y++) {
        if (py + (u32)y >= g_fb.fb_height) break;
        for (int x = 0; x < font->cell_w; x++) {
            if (px + (u32)x >= g_fb.fb_width) break;
            const u8* rgb = glyph_pixel_is_set(font, glyph, x, y) ? fg : bg;
            set_pixel(px + (u32)x, py + (u32)y, rgb);
        }
    }
}

static const psf_font_t* gui_font(void) {
    if (g_has_gui_font) return &g_gui_font;
    if (g_has_console_font) return &g_console_font;
    return NULL;
}

static int cols(void) { return g_has_fb && g_has_console_font ? (int)(g_fb.fb_width / (u32)g_console_font.cell_w) : 0; }
static int rows(void) { return g_has_fb && g_has_console_font ? (int)(g_fb.fb_height / (u32)g_console_font.cell_h) : 0; }

static int load_psf_from_buffer(const void* buf, u32 len, psf_font_t* out) {
    if (!buf || !out || len < sizeof(psf1_header_t)) return 0;
    const psf1_header_t* hdr = (const psf1_header_t*)buf;
    if (hdr->magic0 == 0x36 && hdr->magic1 == 0x04) {
        int glyph_count = (hdr->mode & 0x01) ? 512 : 256;
        u32 glyph_bytes = (u32)glyph_count * (u32)hdr->charsize;
        if (hdr->charsize == 0) return 0;
        if ((u32)sizeof(psf1_header_t) + glyph_bytes > len) return 0;
        out->cell_w = 8;
        out->cell_h = hdr->charsize;
        out->glyph_size = hdr->charsize;
        out->bytes_per_row = 1;
        out->glyph_count = glyph_count;
        out->glyphs = (const u8*)buf + sizeof(psf1_header_t);
        return 1;
    }

    if (len < sizeof(psf2_header_t)) return 0;
    const psf2_header_t* hdr2 = (const psf2_header_t*)buf;
    if (hdr2->magic != 0x864ab572u) return 0;
    if (hdr2->version != 0) return 0;
    if (hdr2->headersize < sizeof(psf2_header_t) || hdr2->headersize > len) return 0;
    if (hdr2->width == 0 || hdr2->height == 0 || hdr2->length == 0 || hdr2->charsize == 0) return 0;
    if (hdr2->width > 64 || hdr2->height > 64) return 0;

    u32 bytes_per_row = (hdr2->width + 7) / 8;
    u32 min_glyph_size = bytes_per_row * hdr2->height;
    if (hdr2->charsize < min_glyph_size) return 0;
    if (hdr2->length > ((len - hdr2->headersize) / hdr2->charsize)) return 0;

    out->cell_w = (int)hdr2->width;
    out->cell_h = (int)hdr2->height;
    out->glyph_size = (int)hdr2->charsize;
    out->bytes_per_row = (int)bytes_per_row;
    out->glyph_count = (int)hdr2->length;
    out->glyphs = (const u8*)buf + hdr2->headersize;
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

static int load_font_from_ramfs(const char* path, psf_font_t* out) {
    if (!g_has_fb || !path) return 0;
    Node* file = cldramfs_resolve_path_file(path, 0);
    if (!file || !file->content || file->content_size < 4) return 0;
    return load_psf_from_buffer(file->content, file->content_size, out);
}

int fb_console_load_psf_from_ramfs(const char* path) {
    if (!load_font_from_ramfs(path, &g_console_font)) return 0;
    g_has_console_font = 1;
    return 1;
}

int fb_gui_load_psf_from_ramfs(const char* path) {
    if (!load_font_from_ramfs(path, &g_gui_font)) return 0;
    g_has_gui_font = 1;
    return 1;
}

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static const char* skip_lua_space(const char* p, const char* end) {
    while (p < end) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
            continue;
        }
        if (p + 1 < end && p[0] == '-' && p[1] == '-') {
            p += 2;
            while (p < end && *p != '\n') p++;
            continue;
        }
        break;
    }
    return p;
}

static int read_lua_string(const char** inout, const char* end, char* out, u32 out_size) {
    const char* p = *inout;
    if (p >= end || (*p != '"' && *p != '\'')) return 0;
    char quote = *p++;
    u32 len = 0;
    while (p < end && *p != quote) {
        char c = *p++;
        if (c == '\\' && p < end) {
            char escaped = *p++;
            if (escaped == 'n') c = '\n';
            else if (escaped == 't') c = '\t';
            else c = escaped;
        }
        if (len + 1 < out_size) {
            out[len++] = c;
        }
    }
    if (p >= end || *p != quote) return 0;
    out[len] = '\0';
    *inout = p + 1;
    return 1;
}

static int read_lua_assignment(const char* data, u32 size, const char* key, char* out, u32 out_size) {
    const char* p = data;
    const char* end = data + size;
    u32 key_len = strlen(key);

    while (p < end) {
        p = skip_lua_space(p, end);
        if (p >= end) break;

        if ((u32)(end - p) >= key_len &&
            strncmp(p, key, key_len) == 0 &&
            (p == data || !is_ident_char(p[-1])) &&
            (p + key_len >= end || !is_ident_char(p[key_len]))) {
            p += key_len;
            p = skip_lua_space(p, end);
            if (p >= end || *p != '=') continue;
            p++;
            p = skip_lua_space(p, end);
            if (read_lua_string(&p, end, out, out_size)) return 1;
        }

        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    return 0;
}

int fb_load_fonts_from_config(const char* path) {
    char console_path[256];
    char gui_path[256];
    const char* default_font = "/usr/fonts/Lat15-Terminus16.psf";

    strcpy(console_path, default_font);
    strcpy(gui_path, default_font);

    Node* file = path ? cldramfs_resolve_path_file(path, 0) : NULL;
    if (file && file->content && file->content_size > 0) {
        (void)read_lua_assignment(file->content, file->content_size, "console_font", console_path, (u32)sizeof(console_path));
        (void)read_lua_assignment(file->content, file->content_size, "gui_font", gui_path, (u32)sizeof(gui_path));
    }

    int console_ok = fb_console_load_psf_from_ramfs(console_path);
    if (!console_ok && strcmp(console_path, default_font) != 0) {
        console_ok = fb_console_load_psf_from_ramfs(default_font);
    }

    int gui_ok = fb_gui_load_psf_from_ramfs(gui_path);
    if (!gui_ok && strcmp(gui_path, default_font) != 0) {
        gui_ok = fb_gui_load_psf_from_ramfs(default_font);
    }
    if (!gui_ok && console_ok) {
        g_gui_font = g_console_font;
        g_has_gui_font = 1;
        gui_ok = 1;
    }

    return console_ok || gui_ok;
}

int fb_console_is_active(void) {
    return g_has_fb && g_has_console_font;
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
    draw_glyph(&g_console_font, (u32)x, (u32)y, (u8)c, fg, bg);
}

void fb_console_scroll_up(u8 vga_attr) {
    if (!fb_console_is_active()) return;
    const u32 row_px = (u32)g_console_font.cell_h;
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
    u32 py = (u32)y * (u32)g_console_font.cell_h;
    fill_rect(0, py, g_fb.fb_width, (u32)g_console_font.cell_h, bg);
}

void fb_console_clear_to_eol(int x, int y, u8 vga_attr) {
    if (!fb_console_is_active()) return;
    if (x < 0 || y < 0) return;
    if (y >= rows()) return;
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    u32 px = (u32)x * (u32)g_console_font.cell_w;
    u32 py = (u32)y * (u32)g_console_font.cell_h;
    if (px >= g_fb.fb_width) return;
    fill_rect(px, py, g_fb.fb_width - px, (u32)g_console_font.cell_h, bg);
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
    const psf_font_t* font = gui_font();
    if (!font) return 0;
    if (out_w) *out_w = font->cell_w;
    if (out_h) *out_h = font->cell_h;
    return 1;
}

int fb_console_font_get_cell_size(int* out_w, int* out_h) {
    if (!g_has_console_font) return 0;
    if (out_w) *out_w = g_console_font.cell_w;
    if (out_h) *out_h = g_console_font.cell_h;
    return 1;
}

void fb_fill_rect_attr(u32 x, u32 y, u32 w, u32 h, u8 vga_attr) {
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    fill_rect(x, y, w, h, bg);
}

void fb_fill_rect_fg(u32 x, u32 y, u32 w, u32 h, u8 vga_attr) {
    const u8* fg = PALETTE[fg_idx(vga_attr) & 0x0F];
    fill_rect(x, y, w, h, fg);
}

void fb_draw_char_px(u32 px, u32 py, char c, u8 vga_attr) {
    const psf_font_t* font = gui_font();
    if (!font || !g_has_fb) return;
    const u8* fg = PALETTE[fg_idx(vga_attr) & 0x0F];
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    u32 idx = (u32)(u8)c;
    if (idx >= (u32)font->glyph_count) idx = (u32)'?';
    if (idx >= (u32)font->glyph_count) idx = 0;
    const u8* glyph = font->glyphs + idx * (u32)font->glyph_size;
    for (int y2 = 0; y2 < font->cell_h; y2++) {
        if (py + (u32)y2 >= g_fb.fb_height) break;
        for (int x2 = 0; x2 < font->cell_w; x2++) {
            if (px + (u32)x2 >= g_fb.fb_width) break;
            const u8* rgb = glyph_pixel_is_set(font, glyph, x2, y2) ? fg : bg;
            set_pixel(px + (u32)x2, py + (u32)y2, rgb);
        }
    }
}

void fb_draw_char_px_scaled(u32 px, u32 py, char c, u8 vga_attr, int scale) {
    const psf_font_t* font = gui_font();
    if (!font || !g_has_fb) return;
    if (scale <= 1) {
        fb_draw_char_px(px, py, c, vga_attr);
        return;
    }
    if (scale > 4) scale = 4;

    const u8* fg = PALETTE[fg_idx(vga_attr) & 0x0F];
    const u8* bg = PALETTE[bg_idx(vga_attr) & 0x0F];
    u32 idx = (u32)(u8)c;
    if (idx >= (u32)font->glyph_count) idx = (u32)'?';
    if (idx >= (u32)font->glyph_count) idx = 0;
    const u8* glyph = font->glyphs + idx * (u32)font->glyph_size;

    for (int y2 = 0; y2 < font->cell_h; y2++) {
        for (int x2 = 0; x2 < font->cell_w; x2++) {
            const u8* rgb = glyph_pixel_is_set(font, glyph, x2, y2) ? fg : bg;
            u32 sx0 = px + (u32)(x2 * scale);
            u32 sy0 = py + (u32)(y2 * scale);
            for (int sy = 0; sy < scale; sy++) {
                if (sy0 + (u32)sy >= g_fb.fb_height) break;
                for (int sx = 0; sx < scale; sx++) {
                    if (sx0 + (u32)sx >= g_fb.fb_width) break;
                    set_pixel(sx0 + (u32)sx, sy0 + (u32)sy, rgb);
                }
            }
        }
    }
}

void fb_draw_char_px_nobg(u32 px, u32 py, char c, u8 fg_index) {
    const psf_font_t* font = gui_font();
    if (!font || !g_has_fb) return;
    const u8* fg = PALETTE[fg_index & 0x0F];
    u32 idx = (u32)(u8)c;
    if (idx >= (u32)font->glyph_count) idx = (u32)'?';
    if (idx >= (u32)font->glyph_count) idx = 0;
    const u8* glyph = font->glyphs + idx * (u32)font->glyph_size;
    for (int y2 = 0; y2 < font->cell_h; y2++) {
        if (py + (u32)y2 >= g_fb.fb_height) break;
        for (int x2 = 0; x2 < font->cell_w; x2++) {
            if (px + (u32)x2 >= g_fb.fb_width) break;
            if (glyph_pixel_is_set(font, glyph, x2, y2)) {
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
