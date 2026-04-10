#include "browser.h"
#include "gui.h"
#include "png.h"
#include <fb/fb_console.h>
#include <cldramfs/cldramfs.h>
#include <kmalloc.h>
#include <ps2.h>
#include <cldramfs/tty.h>
#include <string.h>
#include <stdio.h>

#define BROWSER_FOLDER_ICON "/usr/share/icons/folder.png"
#define BROWSER_FILE_ICON   "/usr/share/icons/file.png"
#define BROWSER_MAX_ITEMS   128
#define BROWSER_PATH_MAX    256
#define BROWSER_TOOL_H      26
#define BROWSER_ICON_W      80
#define BROWSER_ICON_H      70
#define BROWSER_CELL_W      108
#define BROWSER_CELL_H      106
#define BROWSER_MENU_W      78
#define BROWSER_MENU_ITEM_H 18

typedef struct {
    Node *node;
    u32 x;
    u32 y;
    u32 w;
    u32 h;
} browser_item_t;

static u32 b_px = 0, b_py = 0, b_pw = 0, b_ph = 0;
static Node *b_dir = 0;
static Node *b_selected = 0;
static char b_path[BROWSER_PATH_MAX] = "/";
static gui_png_t b_folder_icon;
static gui_png_t b_file_icon;
static int b_icons_loaded = 0;
static u8 *b_icon_row = 0;
static u8 b_bpp = 0;
static browser_item_t b_items[BROWSER_MAX_ITEMS];
static int b_item_count = 0;
static u32 b_up_x = 0, b_up_y = 0, b_up_w = 0, b_up_h = 0;
static u32 b_path_x = 0, b_path_y = 0, b_path_w = 0, b_path_h = 0;
static int b_path_editing = 0;
static int b_context_open = 0;
static u32 b_ctx_x = 0, b_ctx_y = 0, b_ctx_w = 0, b_ctx_h = 0;
static u32 b_ctx_open_x = 0, b_ctx_open_y = 0, b_ctx_open_w = 0, b_ctx_open_h = 0;
static u32 b_ctx_run_x = 0, b_ctx_run_y = 0, b_ctx_run_w = 0, b_ctx_run_h = 0;
static u32 b_ctx_delete_x = 0, b_ctx_delete_y = 0, b_ctx_delete_w = 0, b_ctx_delete_h = 0;

static const u8 BG[3]       = { 0xF2, 0xF2, 0xF0 };
static const u8 TOOL_BG[3]  = { 0xD8, 0xDC, 0xDF };
static const u8 TILE_BG[3]  = { 0xFF, 0xFF, 0xFF };
static const u8 SEL_BG[3]   = { 0xC8, 0xDD, 0xEF };
static const u8 BORDER[3]   = { 0x88, 0x90, 0x98 };
static const u8 FOLDER[3]   = { 0xE1, 0xB4, 0x40 };
static const u8 FILE_BG[3]  = { 0xF8, 0xF8, 0xF8 };

static void draw_rect(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    if (!w || !h) return;
    fb_fill_rect_rgb(x, y, w, h, rgb[0], rgb[1], rgb[2]);
}

static void draw_border(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    if (w < 2 || h < 2) return;
    draw_rect(x, y, w, 1, rgb);
    draw_rect(x, y + h - 1, w, 1, rgb);
    draw_rect(x, y, 1, h, rgb);
    draw_rect(x + w - 1, y, 1, h, rgb);
}

static void draw_text_clip(u32 x, u32 y, const char *s, u8 attr, u32 max_w) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u8 fg = attr & 0x0F;
    u32 used = 0;
    while (s && *s && used + (u32)cw <= max_w) {
        fb_draw_char_px_nobg(x + used, y, *s, fg);
        used += (u32)cw;
        s++;
    }
}

static u32 text_width_px(const char *s) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0;
    while (s && s[n]) n++;
    return n * (u32)cw;
}

static void draw_text_centered(u32 x, u32 y, u32 w, const char *s, u8 attr) {
    u32 tw = text_width_px(s);
    if (tw + 8 <= w) {
        draw_text_clip(x + (w - tw) / 2, y, s, attr, tw);
    } else {
        draw_text_clip(x + 4, y, s, attr, w > 8 ? w - 8 : w);
    }
}

static int point_in(u32 x, u32 y, u32 rx, u32 ry, u32 rw, u32 rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void browser_build_path(void) {
    b_path[0] = '/';
    b_path[1] = '\0';
    if (!b_dir || b_dir == ramfs_root) return;

    Node *nodes[32];
    int depth = 0;
    Node *cur = b_dir;
    while (cur && cur != ramfs_root && depth < 32) {
        nodes[depth++] = cur;
        cur = cur->parent;
    }
    for (int i = depth - 1; i >= 0; i--) {
        if (!nodes[i] || !nodes[i]->name) continue;
        if (strlen(b_path) > 1) strncat(b_path, "/", sizeof(b_path) - strlen(b_path) - 1);
        strncat(b_path, nodes[i]->name, sizeof(b_path) - strlen(b_path) - 1);
    }
}

static void browser_path_for_node(Node *node, char *out, u32 out_size) {
    if (!out || out_size == 0) return;
    out[0] = '/';
    if (out_size > 1) out[1] = '\0';
    if (!node || node == ramfs_root) return;

    Node *nodes[32];
    int depth = 0;
    Node *cur = node;
    while (cur && cur != ramfs_root && depth < 32) {
        nodes[depth++] = cur;
        cur = cur->parent;
    }
    for (int i = depth - 1; i >= 0; i--) {
        if (!nodes[i] || !nodes[i]->name) continue;
        if (strlen(out) > 1) strncat(out, "/", out_size - strlen(out) - 1);
        strncat(out, nodes[i]->name, out_size - strlen(out) - 1);
    }
}

static int browser_path_is_lua(const char *path) {
    u32 len = path ? strlen(path) : 0;
    return len >= 4 && strcmp(path + len - 4, ".lua") == 0;
}

static int browser_file_is_text(Node *node) {
    if (!node || node->type != FILE_NODE || !node->content) return 0;
    const u8 *d = (const u8*)node->content;
    u32 n = node->content_size;
    if (n >= 8 && d[0] == 137 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return 0;
    if (n >= 4 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F') return 0;
    for (u32 i = 0; i < n && i < 512; i++) {
        u8 c = d[i];
        if (c == 0) return 0;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != 0x1B) return 0;
    }
    return 1;
}

static void browser_delete_node(Node *node) {
    if (!node || !node->parent) return;
    Node *parent = node->parent;
    for (u32 i = 0; i < parent->child_count; i++) {
        if (parent->children[i] != node) continue;
        for (u32 j = i; j + 1 < parent->child_count; j++) {
            parent->children[j] = parent->children[j + 1];
        }
        parent->child_count--;
        cldramfs_free_node(node);
        return;
    }
}

static int browser_load_icon(const char *path, gui_png_t *out) {
    Node *f = cldramfs_resolve_path_file(path, 0);
    if (!f || f->type != FILE_NODE || !f->content || !f->content_size) return 0;
    return gui_png_load(f->content, f->content_size, out);
}

static void browser_load_icons(void) {
    if (b_icons_loaded) return;
    b_icons_loaded = 1;
    (void)browser_load_icon(BROWSER_FOLDER_ICON, &b_folder_icon);
    (void)browser_load_icon(BROWSER_FILE_ICON, &b_file_icon);
}

static void browser_draw_png(const gui_png_t *png, u32 x, u32 y, const u8 bg[3]) {
    if (!png || !png->rgba || !b_icon_row || !b_bpp) return;
    u32 draw_w = png->width;
    u32 draw_h = png->height;
    if (draw_w > BROWSER_ICON_W) draw_w = BROWSER_ICON_W;
    if (draw_h > BROWSER_ICON_H) draw_h = BROWSER_ICON_H;

    for (u32 yy = 0; yy < draw_h; yy++) {
        u8 *d = b_icon_row;
        for (u32 xx = 0; xx < draw_w; xx++) {
            u8 rgba[4];
            gui_png_get_rgba(png, xx, yy, rgba);
            gui_png_write_fb_pixel_rgba(d, b_bpp, rgba, bg);
            d += b_bpp;
        }
        fb_blit(x, y + yy, draw_w, 1, b_icon_row);
    }
}

static void browser_draw_fallback_icon(u32 x, u32 y, int is_dir) {
    if (is_dir) {
        draw_rect(x + 4, y + 10, 28, 10, FOLDER);
        draw_rect(x + 4, y + 18, 72, 46, FOLDER);
        draw_border(x + 4, y + 18, 72, 46, BORDER);
    } else {
        draw_rect(x + 14, y + 4, 52, 62, FILE_BG);
        draw_border(x + 14, y + 4, 52, 62, BORDER);
        draw_rect(x + 24, y + 22, 32, 1, BORDER);
        draw_rect(x + 24, y + 34, 32, 1, BORDER);
        draw_rect(x + 24, y + 46, 24, 1, BORDER);
    }
}

static void browser_draw_item(Node *node, u32 x, u32 y, u32 w, u32 h) {
    int selected = node == b_selected;
    draw_rect(x, y, w, h, selected ? SEL_BG : BG);
    if (selected) draw_border(x, y, w, h, BORDER);

    u32 ix = x + (w > BROWSER_ICON_W ? (w - BROWSER_ICON_W) / 2 : 0);
    u32 iy = y + 4;
    const gui_png_t *icon = node->type == DIR_NODE ? &b_folder_icon : &b_file_icon;
    if (icon->rgba) browser_draw_png(icon, ix, iy, selected ? SEL_BG : BG);
    else browser_draw_fallback_icon(ix, iy, node->type == DIR_NODE);

    u32 label_y = y + BROWSER_ICON_H + 8;
    draw_text_centered(x, label_y, w, node->name ? node->name : "?", 0x00);
}

static void browser_draw_context_menu(void) {
    if (!b_context_open || !b_selected) return;
    b_ctx_w = BROWSER_MENU_W;
    b_ctx_h = BROWSER_MENU_ITEM_H * 3;
    if (b_ctx_x + b_ctx_w > b_px + b_pw) b_ctx_x = b_px + b_pw - b_ctx_w;
    if (b_ctx_y + b_ctx_h > b_py + b_ph) b_ctx_y = b_py + b_ph - b_ctx_h;

    draw_rect(b_ctx_x, b_ctx_y, b_ctx_w, b_ctx_h, TILE_BG);
    draw_border(b_ctx_x, b_ctx_y, b_ctx_w, b_ctx_h, BORDER);

    b_ctx_open_x = b_ctx_x;
    b_ctx_open_y = b_ctx_y;
    b_ctx_open_w = b_ctx_w;
    b_ctx_open_h = BROWSER_MENU_ITEM_H;
    b_ctx_run_x = b_ctx_x;
    b_ctx_run_y = b_ctx_y + BROWSER_MENU_ITEM_H;
    b_ctx_run_w = b_ctx_w;
    b_ctx_run_h = BROWSER_MENU_ITEM_H;
    b_ctx_delete_x = b_ctx_x;
    b_ctx_delete_y = b_ctx_y + BROWSER_MENU_ITEM_H * 2;
    b_ctx_delete_w = b_ctx_w;
    b_ctx_delete_h = BROWSER_MENU_ITEM_H;

    draw_text_clip(b_ctx_open_x + 6, b_ctx_open_y + 2, "Open", 0x00, b_ctx_open_w - 10);
    draw_text_clip(b_ctx_run_x + 6, b_ctx_run_y + 2, "Run", 0x00, b_ctx_run_w - 10);
    draw_text_clip(b_ctx_delete_x + 6, b_ctx_delete_y + 2, "Delete", 0x04, b_ctx_delete_w - 10);
}

void gui_browser_init(u32 px, u32 py, u32 pw, u32 ph) {
    b_px = px; b_py = py; b_pw = pw; b_ph = ph;
    b_bpp = fb_get_bytespp();
    if (!b_icon_row && b_bpp) b_icon_row = (u8*)kmalloc(BROWSER_ICON_W * b_bpp);
    browser_load_icons();
    b_dir = ramfs_root;
    b_selected = 0;
    browser_build_path();
}

void gui_browser_move(u32 px, u32 py) {
    b_px = px; b_py = py;
}

void gui_browser_resize(u32 pw, u32 ph) {
    b_pw = pw; b_ph = ph;
}

void gui_browser_render_all(void) {
    b_item_count = 0;
    if (!b_pw || !b_ph) return;
    if (!b_dir) b_dir = ramfs_root;
    if (!b_path_editing) browser_build_path();

    draw_rect(b_px, b_py, b_pw, b_ph, BG);
    draw_rect(b_px, b_py, b_pw, BROWSER_TOOL_H, TOOL_BG);
    draw_border(b_px, b_py, b_pw, BROWSER_TOOL_H, BORDER);

    b_up_x = b_px + 6;
    b_up_y = b_py + 4;
    b_up_w = 34;
    b_up_h = 18;
    draw_rect(b_up_x, b_up_y, b_up_w, b_up_h, b_dir && b_dir != ramfs_root ? TILE_BG : TOOL_BG);
    draw_border(b_up_x, b_up_y, b_up_w, b_up_h, BORDER);
    draw_text_clip(b_up_x + 7, b_up_y + 1, "Up", 0x00, b_up_w - 10);
    b_path_x = b_px + 48;
    b_path_y = b_py + 4;
    b_path_w = b_pw > 56 ? b_pw - 56 : 0;
    b_path_h = 18;
    draw_rect(b_path_x, b_path_y, b_path_w, b_path_h, b_path_editing ? TILE_BG : TOOL_BG);
    draw_border(b_path_x, b_path_y, b_path_w, b_path_h, BORDER);
    draw_text_clip(b_path_x + 5, b_path_y + 1, b_path, 0x00, b_path_w > 10 ? b_path_w - 10 : 0);

    if (!b_dir || b_dir->type != DIR_NODE) return;

    u32 cols = b_pw / BROWSER_CELL_W;
    if (cols == 0) cols = 1;
    u32 grid_y = b_py + BROWSER_TOOL_H + 8;
    for (u32 i = 0; i < b_dir->child_count && b_item_count < BROWSER_MAX_ITEMS; i++) {
        Node *child = b_dir->children[i];
        if (!child) continue;
        u32 item = (u32)b_item_count;
        u32 col = item % cols;
        u32 row = item / cols;
        u32 x = b_px + col * BROWSER_CELL_W + 4;
        u32 y = grid_y + row * BROWSER_CELL_H;
        if (y + BROWSER_CELL_H > b_py + b_ph) break;

        b_items[b_item_count].node = child;
        b_items[b_item_count].x = x;
        b_items[b_item_count].y = y;
        b_items[b_item_count].w = BROWSER_CELL_W - 8;
        b_items[b_item_count].h = BROWSER_CELL_H - 4;
        browser_draw_item(child, x, y, BROWSER_CELL_W - 8, BROWSER_CELL_H - 4);
        b_item_count++;
    }

    browser_draw_context_menu();
}

void gui_browser_free(void) {
    gui_png_free(&b_folder_icon);
    gui_png_free(&b_file_icon);
    if (b_icon_row) {
        kfree(b_icon_row);
        b_icon_row = 0;
    }
    b_icons_loaded = 0;
    b_dir = 0;
    b_selected = 0;
    b_item_count = 0;
}

int gui_browser_on_click(u32 px, u32 py) {
    if (b_context_open) {
        if (point_in(px, py, b_ctx_open_x, b_ctx_open_y, b_ctx_open_w, b_ctx_open_h)) {
            char path[BROWSER_PATH_MAX];
            browser_path_for_node(b_selected, path, sizeof(path));
            if (b_selected->type == DIR_NODE) {
                b_dir = b_selected;
                b_selected = 0;
            } else if (browser_file_is_text(b_selected)) {
                gui_open_editor_file(path);
            }
            b_context_open = 0;
            return 1;
        }
        if (point_in(px, py, b_ctx_run_x, b_ctx_run_y, b_ctx_run_w, b_ctx_run_h)) {
            char path[BROWSER_PATH_MAX];
            browser_path_for_node(b_selected, path, sizeof(path));
            if (b_selected->type == FILE_NODE && browser_path_is_lua(path)) gui_run_lua_in_terminal(path);
            b_context_open = 0;
            return 1;
        }
        if (point_in(px, py, b_ctx_delete_x, b_ctx_delete_y, b_ctx_delete_w, b_ctx_delete_h)) {
            browser_delete_node(b_selected);
            b_selected = 0;
            b_context_open = 0;
            return 1;
        }
        b_context_open = 0;
        return 1;
    }

    if (point_in(px, py, b_path_x, b_path_y, b_path_w, b_path_h)) {
        b_path_editing = 1;
        return 1;
    }
    b_path_editing = 0;

    if (point_in(px, py, b_up_x, b_up_y, b_up_w, b_up_h)) {
        if (b_dir && b_dir->parent) {
            b_dir = b_dir->parent;
            b_selected = 0;
            return 1;
        }
        return 0;
    }

    for (int i = 0; i < b_item_count; i++) {
        browser_item_t *item = &b_items[i];
        if (!point_in(px, py, item->x, item->y, item->w, item->h)) continue;
        if (item->node->type == DIR_NODE) {
            b_dir = item->node;
            b_selected = 0;
        } else {
            b_selected = item->node;
        }
        return 1;
    }
    return 0;
}

int gui_browser_on_right_click(u32 px, u32 py) {
    b_path_editing = 0;
    for (int i = 0; i < b_item_count; i++) {
        browser_item_t *item = &b_items[i];
        if (!point_in(px, py, item->x, item->y, item->w, item->h)) continue;
        b_selected = item->node;
        b_context_open = 1;
        b_ctx_x = px;
        b_ctx_y = py;
        return 1;
    }
    if (b_context_open || b_selected) {
        b_context_open = 0;
        b_selected = 0;
        return 1;
    }
    return 0;
}

int gui_browser_on_move(u32 px, u32 py) {
    (void)px; (void)py;
    return 0;
}

void gui_browser_handle_key(u8 scancode, int is_extended, int is_pressed) {
    if (!is_pressed) return;
    u128 ka = ps2_keyarr();
    int shift = (ka & ((u128)1 << 0x2A)) || (ka & ((u128)1 << 0x36));

    if (b_path_editing) {
        if (is_extended) return;
        if (scancode == US_ESC) {
            b_path_editing = 0;
            browser_build_path();
            return;
        }
        if (scancode == US_ENTER) {
            Node *dir = cldramfs_resolve_path_dir(b_path, 0);
            if (dir && dir->type == DIR_NODE) {
                b_dir = dir;
                b_selected = 0;
            }
            b_path_editing = 0;
            browser_build_path();
            return;
        }
        if (scancode == US_BACKSPACE) {
            u32 len = strlen(b_path);
            if (len > 0) b_path[len - 1] = '\0';
            return;
        }
        if (is_printable_key(scancode)) {
            u32 len = strlen(b_path);
            if (len + 1 < sizeof(b_path)) {
                char c = scancode_to_char(scancode, shift);
                if (c) {
                    b_path[len] = c;
                    b_path[len + 1] = '\0';
                }
            }
        }
        return;
    }

    if ((scancode == US_BACKSPACE || (is_extended && scancode == US_ARROW_LEFT)) && b_dir && b_dir->parent) {
        b_dir = b_dir->parent;
        b_selected = 0;
    }
}
