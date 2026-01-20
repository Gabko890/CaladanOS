#include "viewer.h"
#include <fb/fb_console.h>
#include <cldramfs/cldramfs.h>
#include <kmalloc.h>
#include <string.h>
#include <ps2.h>
#include <cldramfs/tty.h>

// Simple BMP viewer that scales image to fit given region.
// Supports uncompressed 24bpp and 32bpp BMP (BI_RGB, and 32bpp bitfields).

// Viewer region
static u32 v_px = 0, v_py = 0, v_pw = 0, v_ph = 0;
static u8  v_fb_bpp = 0;

// Source BMP metadata (points into ramfs-backed content)
static const u8* v_img_base = 0; // pointer to BMP start ('B''M')
static u32 v_img_pix_off = 0;    // pixel array offset
static u32 v_img_w = 0;          // source width
static u32 v_img_h = 0;          // source height (absolute value)
static u16 v_img_bpp = 0;        // bits per pixel in source (24 or 32)
static u32 v_img_stride = 0;     // bytes per source row (padded to 4 bytes)
static int v_img_top_down = 0;   // 1 if top-down bitmap

// Temporary line buffer (one scanline in framebuffer format)
static u8* v_linebuf = 0;        // size: v_pw * v_fb_bpp
static int v_new_image = 0;

// Titlebar/Open menu UI state
static u32 v_tb_x = 0, v_tb_y = 0, v_tb_w = 0, v_tb_h = 0;
static int v_menu_open = 0;
static u32 v_open_btn_x = 0, v_open_btn_y = 0, v_open_btn_w = 0, v_open_btn_h = 0;
static u32 v_menu_x = 0, v_menu_y = 0, v_menu_w = 0, v_menu_h = 0;
static u32 v_mi_open_x = 0, v_mi_open_y = 0, v_mi_open_w = 0, v_mi_open_h = 0;

// Modal input state for entering a path
static int v_modal_open = 0;
static char v_modal_buf[128];
static int v_modal_len = 0;

#define V_MAX_ITEMS 32
static char* v_list_paths[V_MAX_ITEMS];
static char* v_list_labels[V_MAX_ITEMS];
static int v_list_count = 0;

static void viewer_clear_list(void) { for (int i = 0; i < v_list_count; i++) { if (v_list_paths[i]) kfree(v_list_paths[i]); if (v_list_labels[i]) kfree(v_list_labels[i]); } v_list_count = 0; }

static inline u32 rd_le32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u16 rd_le16(const u8* p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}

// General row stride calculator: rows are padded to 4-byte boundaries.
static inline u32 bmp_stride(u32 w, u32 bits_per_pixel) {
    u64 bits = (u64)w * (u64)bits_per_pixel;
    u64 stride = ((bits + 31) / 32) * 4;
    return (u32)stride;
}

static int viewer_load_bmp(const char* path) {
    v_img_base = 0; v_img_pix_off = 0; v_img_w = v_img_h = 0; v_img_bpp = 0; v_img_stride = 0; v_img_top_down = 0;
    if (!path || !fb_console_present()) return 0;
    Node* f = cldramfs_resolve_path_file(path, 0);
    if (!f || !f->content || f->content_size < 54) return 0;
    const u8* p = (const u8*)f->content;
    if (!(p[0] == 'B' && p[1] == 'M')) return 0;
    u32 pix_off = rd_le32(p + 10);
    u32 dib_size = rd_le32(p + 14);
    if (dib_size < 40) return 0;
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
    if (!(compression == 0 || (bpp == 32 && compression == 3))) return 0;
    if (w == 0 || h == 0) return 0;
    if (pix_off >= f->content_size) return 0;
    // Quick bounds safety
    u32 stride = bmp_stride(w, bpp);
    if ((u64)pix_off + (u64)stride * (u64)(h - 1) + (u64)(w * (bpp / 8)) > (u64)f->content_size) {
        if ((u64)pix_off >= (u64)f->content_size) return 0;
    }
    v_img_base = p;
    v_img_pix_off = pix_off;
    v_img_w = w;
    v_img_h = h;
    v_img_bpp = bpp;
    v_img_stride = stride;
    v_img_top_down = top_down;
    return 1;
}

static inline const u8* viewer_src_row_ptr(u32 src_row) {
    return v_img_base + v_img_pix_off + (u64)src_row * (u64)v_img_stride;
}

static void viewer_build_scaled_row_into(u32 dst_y, u8* out_row) {
    if (!v_img_base || v_pw == 0 || v_ph == 0) return;
    u32 sy = (u64)dst_y * (u64)v_img_h / (u64)v_ph;
    u32 src_row = v_img_top_down ? sy : (v_img_h - 1 - sy);
    const u8* row_ptr = viewer_src_row_ptr(src_row);
    u32 src_px_stride = (u32)(v_img_bpp / 8);
    u8* d = out_row;
    for (u32 dx = 0; dx < v_pw; dx++) {
        u32 sx = (u64)dx * (u64)v_img_w / (u64)v_pw;
        const u8* sp = row_ptr + (u64)sx * (u64)src_px_stride;
        u8 B = sp[0];
        u8 G = sp[1];
        u8 R = sp[2];
        if (v_fb_bpp == 4) {
            d[0] = B; d[1] = G; d[2] = R; d[3] = 0xFF;
        } else if (v_fb_bpp == 3) {
            d[0] = B; d[1] = G; d[2] = R;
        } else if (v_fb_bpp == 2) {
            u16 rv = ((u16)R & 0xF8) << 8;
            u16 gv = ((u16)G & 0xFC) << 3;
            u16 bv = ((u16)B & 0xF8) >> 3;
            u16 v = rv | gv | bv;
            d[0] = (u8)(v & 0xFF);
            d[1] = (u8)(v >> 8);
        }
        d += v_fb_bpp;
    }
}

void gui_viewer_init(u32 px, u32 py, u32 pw, u32 ph) {
    v_px = px; v_py = py; v_pw = pw; v_ph = ph;
    v_fb_bpp = fb_get_bytespp();
    if (v_linebuf) { kfree(v_linebuf); v_linebuf = 0; }
    if (v_pw && v_fb_bpp) {
        v_linebuf = (u8*)kmalloc((size_t)((u64)v_pw * (u64)v_fb_bpp));
    }
    // Start blank; user must open a file
    v_img_base = 0; v_img_pix_off = 0; v_img_w = v_img_h = 0; v_img_bpp = 0; v_img_stride = 0; v_img_top_down = 0;
    v_new_image = 0; v_menu_open = 0; viewer_clear_list(); v_modal_open = 0; v_modal_len = 0; v_modal_buf[0] = '\0';
}

void gui_viewer_move(u32 px, u32 py) {
    v_px = px; v_py = py;
}

void gui_viewer_resize(u32 pw, u32 ph) {
    v_pw = pw; v_ph = ph;
    if (v_linebuf) { kfree(v_linebuf); v_linebuf = 0; }
    if (v_pw && v_fb_bpp) {
        v_linebuf = (u8*)kmalloc((size_t)((u64)v_pw * (u64)v_fb_bpp));
    }
}

void gui_viewer_render_all(void) {
    if (!v_fb_bpp || !v_linebuf || !v_pw || !v_ph) return;
    // If no image loaded, fill region with white background
    if (!v_img_base) {
        fb_fill_rect_rgb(v_px, v_py, v_pw, v_ph, 0xFF, 0xFF, 0xFF);
        return;
    }
    for (u32 y = 0; y < v_ph; y++) {
        viewer_build_scaled_row_into(y, v_linebuf);
        fb_blit(v_px, v_py + y, v_pw, 1, v_linebuf);
    }
}

void gui_viewer_free(void) {
    if (v_linebuf) { kfree(v_linebuf); v_linebuf = 0; }
    v_img_base = 0; v_img_pix_off = 0; v_img_w = v_img_h = 0; v_img_bpp = 0; v_img_stride = 0; v_img_top_down = 0;
    v_pw = v_ph = 0;
    viewer_clear_list();
}

// UI helpers
static void viewer_draw_text(u32 x, u32 y, const char* s, u8 vga_attr) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u32 px = x;
    for (; *s; s++) {
        u8 fg = vga_attr & 0x0F;
        fb_draw_char_px_nobg(px, y, *s, fg);
        px += (u32)cw;
    }
}

static u32 viewer_text_width_px(const char* s) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0; while (s && s[n]) n++;
    return (u32)cw * n;
}

void gui_viewer_set_titlebar(u32 win_x, u32 win_y, u32 win_w, u32 title_h) {
    v_tb_x = win_x + 2;
    v_tb_y = win_y + 2;
    v_tb_w = win_w - 4;
    v_tb_h = title_h;
    // Position File button at left, vertically centered inside titlebar
    const char* lbl = "File";
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 tw = viewer_text_width_px(lbl) + 12;
    v_open_btn_w = tw;
    // Keep 2px vertical padding above and below (like editor), clamp to title height
    v_open_btn_h = (v_tb_h > 4) ? (v_tb_h - 4) : v_tb_h;
    v_open_btn_x = v_tb_x + 6;
    // Center button vertically in titlebar
    u32 vpad = (v_tb_h > v_open_btn_h) ? ((v_tb_h - v_open_btn_h) / 2) : 0;
    v_open_btn_y = v_tb_y + vpad;
}

void gui_viewer_draw_overlays(void) {
    // Draw File button (Editor-style subtle button)
    u8 btn_col[3] = { 0x40, 0x40, 0x44 };
    if (v_open_btn_w && v_open_btn_h) fb_fill_rect_rgb(v_open_btn_x, v_open_btn_y, v_open_btn_w, v_open_btn_h, btn_col[0], btn_col[1], btn_col[2]);
    // Vertically center text inside the button
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 ty = v_open_btn_y + (v_open_btn_h > (u32)ch ? ((v_open_btn_h - (u32)ch) / 2) : 0);
    viewer_draw_text(v_open_btn_x + 6, ty, "File", 0x0F);
    // Dropdown under titlebar, Editor-style
    if (v_menu_open) {
        const char* i1 = "Open...";
        u32 w1 = viewer_text_width_px(i1) + 12;
        u32 mx = v_open_btn_x; u32 my = v_tb_y + v_tb_h; // directly under titlebar
        u32 ih = 18;
        // Slight contrast dropdown
        fb_fill_rect_rgb(mx, my, w1, ih, 0x40, 0x40, 0x44);
        viewer_draw_text(mx + 6, my + 2, i1, 0x0F);
        v_mi_open_x = mx; v_mi_open_y = my; v_mi_open_w = w1; v_mi_open_h = ih;
        v_menu_x = mx; v_menu_y = my; v_menu_w = w1; v_menu_h = ih;
    } else {
        v_mi_open_x = v_mi_open_y = v_mi_open_w = v_mi_open_h = 0;
        v_menu_x = v_menu_y = v_menu_w = v_menu_h = 0;
    }
    // Modal prompt
    if (v_modal_open) {
        // Center a small modal in the viewer area
        u32 mw = 360, mh = 40;
        u32 cx = v_px + (v_pw > mw ? (v_pw - mw) / 2 : 0);
        u32 cy = v_py + (v_ph > mh ? (v_ph - mh) / 2 : 0);
        // Slight dark box with border
        fb_fill_rect_rgb(cx, cy, mw, mh, 0x33, 0x33, 0x36);
        fb_fill_rect_rgb(cx, cy, mw, 1, 0x77,0x77,0x77);
        fb_fill_rect_rgb(cx, cy + mh - 1, mw, 1, 0x77,0x77,0x77);
        fb_fill_rect_rgb(cx, cy, 1, mh, 0x77,0x77,0x77);
        fb_fill_rect_rgb(cx + mw - 1, cy, 1, mh, 0x77,0x77,0x77);
        viewer_draw_text(cx + 8, cy + 6, "Open path:", 0x0F);
        viewer_draw_text(cx + 8, cy + 20, v_modal_buf, 0x0F);
    }
}

int gui_viewer_on_click(u32 px, u32 py) {
    int changed = 0;
    // Toggle Open menu on button click
    if (px >= v_open_btn_x && px < v_open_btn_x + v_open_btn_w &&
        py >= v_open_btn_y && py < v_open_btn_y + v_open_btn_h) {
        v_menu_open = !v_menu_open; changed = 1; return changed;
    }
    // Handle dropdown selection or outside click
    if (v_menu_open) {
        if (px >= v_mi_open_x && px < v_mi_open_x + v_mi_open_w &&
            py >= v_mi_open_y && py < v_mi_open_y + v_mi_open_h) {
            v_menu_open = 0; v_modal_open = 1; v_modal_len = 0; v_modal_buf[0] = '\0'; changed = 1; return changed;
        }
        // click outside of menu and button closes menu
        if (!(px >= v_menu_x && px < v_menu_x + v_menu_w && py >= v_menu_y && py < v_menu_y + v_menu_h)) {
            if (!(px >= v_open_btn_x && px < v_open_btn_x + v_open_btn_w && py >= v_open_btn_y && py < v_open_btn_y + v_open_btn_h)) {
                v_menu_open = 0; changed = 1; return changed;
            }
        }
    }
    return changed;
}

int gui_viewer_on_move(u32 px, u32 py) { (void)px; (void)py; return 0; }

int gui_viewer_has_image(void) { return v_img_base != 0; }

void gui_viewer_get_image_dims(u32* w, u32* h) {
    if (w) *w = v_img_w;
    if (h) *h = v_img_h;
}

int gui_viewer_consume_new_image_flag(void) {
    int r = v_new_image; v_new_image = 0; return r;
}

void gui_viewer_handle_key(u8 scancode, int is_extended, int is_pressed) {
    (void)is_extended;
    if (!is_pressed) return;
    if (!v_modal_open) return;
    u128 ka = ps2_keyarr();
    int shift = (ka & ((u128)1 << 0x2A)) || (ka & ((u128)1 << 0x36));
    switch (scancode) {
        case US_ESC:
            v_modal_open = 0;
            gui_viewer_render_all(); gui_viewer_draw_overlays();
            return;
        case US_ENTER: {
            // Try to open path in buffer
            if (v_modal_len > 0) {
                if (viewer_load_bmp(v_modal_buf)) { v_new_image = 1; }
            }
            v_modal_open = 0;
            gui_viewer_render_all(); gui_viewer_draw_overlays();
            return;
        }
        case US_BACKSPACE:
            if (v_modal_len > 0) { v_modal_len--; v_modal_buf[v_modal_len] = '\0'; gui_viewer_render_all(); gui_viewer_draw_overlays(); }
            return;
        default: {
            if (is_printable_key(scancode) && v_modal_len < (int)sizeof(v_modal_buf) - 1) {
                char c = scancode_to_char(scancode, shift);
                if (c) { v_modal_buf[v_modal_len++] = c; v_modal_buf[v_modal_len] = '\0'; gui_viewer_render_all(); gui_viewer_draw_overlays(); }
            }
            return;
        }
    }
}

void gui_viewer_hide_open_button(void) { /* no-op: viewer keeps Open button visible */ v_modal_open = 0; }
