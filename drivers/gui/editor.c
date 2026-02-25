#include "editor.h"
#include <fb/fb_console.h>
#include <kmalloc.h>
#include <ps2.h>
#include <string.h>
#include <cldramfs/cldramfs.h>
#include <cldramfs/tty.h>

// Very simple text editor rendered in a pixel rect using framebuffer font

static u32 e_x = 0, e_y = 0, e_w = 0, e_h = 0; // pixel rect
static int base_cell_w = 8, base_cell_h = 16;
static int cell_w = 8, cell_h = 16;
static int cols = 0, rows = 0, view_cols = 0, view_rows = 0;
static int cur_x = 0, cur_y = 0;
static int doc_end_x = 0, doc_end_y = 0;
static int scroll_x = 0, scroll_y = 0;
static u8 cur_attr = 0x07; // white on black
static int editor_zoom = 1;
static u32 text_x0 = 0;

typedef struct { char ch; } editor_cell_t;
static editor_cell_t* cells = 0; // document rows*cols
static int caret_drawn = 0;
static int caret_x = 0, caret_y = 0;

// Forward declarations for helpers used before their definitions
static int editor_line_end_x(int y);
static void editor_render_all(void);

#define EDITOR_TITLE_BTN_PAD 6
#define EDITOR_LINE_NUMBER_COLS 5
#define EDITOR_MAX_COLS 512
#define EDITOR_MAX_ROWS 512

// Menu/dropdown and modal state
static int menu_open = 0;
static u32 file_btn_x = 0, file_btn_y = 0, file_btn_w = 0, file_btn_h = 0;
static u32 menu_x = 0, menu_y = 0, menu_w = 0, menu_h = 0;
static u32 mi_open_x = 0, mi_open_y = 0, mi_open_w = 0, mi_open_h = 0;
static u32 mi_new_x = 0, mi_new_y = 0, mi_new_w = 0, mi_new_h = 0;
static u32 mi_save_x = 0, mi_save_y = 0, mi_save_w = 0, mi_save_h = 0;
// Titlebar geometry (provided by GUI)
static u32 tb_x = 0, tb_y = 0, tb_w = 0, tb_h = 0;

static char* current_path = 0;

typedef enum { MODAL_NONE = 0, MODAL_OPEN, MODAL_NEW, MODAL_SAVE } modal_t;
static modal_t modal_state = MODAL_NONE;
static char modal_buf[128];
static int modal_len = 0;

// Text helpers
static void draw_text(u32 px, u32 py, const char* s, u8 fg_index) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 x = px;
    for (const char* p = s; p && *p; ++p) {
        fb_draw_char_px_nobg(x, py, *p, fg_index);
        x += (u32)cw;
    }
}
static u32 text_width_px(const char* s) {
    int cw = 8, ch = 16; (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0; while (s && s[n]) n++;
    return (u32)cw * n;
}

static u32 text_y0 = 0; // pixel y of first text row

static inline u8 invert_attr(u8 a) { return (u8)(((a & 0x0F) << 4) | ((a >> 4) & 0x0F)); }

static int editor_path_is_lua(void) {
    if (!current_path) return 0;
    u32 len = strlen(current_path);
    return len >= 4 && strcmp(current_path + len - 4, ".lua") == 0;
}

static char editor_visible_char(char ch) {
    u8 c = (u8)ch;
    if (c == 0x1B) return '^';
    if (c == '\t') return ' ';
    if (c < 32 || c == 127) return '.';
    return ch;
}

static int is_lua_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_lua_ident(char c) {
    return is_lua_ident_start(c) || (c >= '0' && c <= '9');
}

static int lua_keyword_len(const char* s, int len) {
    static const char* keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "if", "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while"
    };
    for (u32 i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if ((int)strlen(keywords[i]) == len && strncmp(s, keywords[i], (size_t)len) == 0) {
            return 1;
        }
    }
    return 0;
}

static u8 editor_lua_attr_at(int x, int y) {
    if (!editor_path_is_lua() || y < 0 || y >= rows || x < 0 || x >= cols) return cur_attr;

    int comment_from = -1;
    int string_from = -1;
    int string_to = -1;
    char quote = 0;

    for (int i = 0; i < cols; i++) {
        char ch = cells[y * cols + i].ch;
        char next = (i + 1 < cols) ? cells[y * cols + i + 1].ch : '\0';

        if (comment_from >= 0) break;

        if (quote) {
            if (ch == '\\' && i + 1 < cols) {
                i++;
                continue;
            }
            if (ch == quote) {
                string_to = i;
                quote = 0;
                if (x >= string_from && x <= string_to) return 0x0E;
            }
            if (i == x) return 0x0E;
            continue;
        }

        if (ch == '-' && next == '-') {
            comment_from = i;
            break;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            string_from = i;
            if (i == x) return 0x0E;
            continue;
        }
    }

    if (comment_from >= 0 && x >= comment_from) return 0x0A;
    if (quote && x >= string_from) return 0x0E;

    char ch = cells[y * cols + x].ch;
    if (ch >= '0' && ch <= '9') return 0x0B;
    if (!is_lua_ident_start(ch)) return cur_attr;

    int start = x;
    while (start > 0 && is_lua_ident(cells[y * cols + start - 1].ch)) start--;
    int end = x;
    while (end < cols && is_lua_ident(cells[y * cols + end].ch)) end++;
    if (lua_keyword_len(&cells[y * cols + start].ch, end - start)) return 0x0D;
    return cur_attr;
}

static void editor_draw_cell(int x, int y) {
    if (!cells) return;
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    int view_x = x - scroll_x;
    int view_y = y - scroll_y;
    if (view_x < 0 || view_x >= view_cols) return;
    if (view_y < 0 || view_y >= view_rows) return;
    char ch = cells[y * cols + x].ch;
    u8 attr = editor_lua_attr_at(x, y);
    u32 px = text_x0 + (u32)view_x * (u32)cell_w;
    u32 py = text_y0 + (u32)view_y * (u32)cell_h;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, cur_attr);
    if (ch != ' ') fb_draw_char_px_scaled(px, py, editor_visible_char(ch), attr, editor_zoom);
}

static void editor_caret_undraw(void) {
    if (!caret_drawn) return;
    editor_draw_cell(caret_x, caret_y);
    caret_drawn = 0;
}

static void editor_caret_draw(void) {
    if (!cells) return;
    if (cur_x < 0 || cur_y < 0 || cur_x >= cols || cur_y >= rows) return;
    int view_x = cur_x - scroll_x;
    int view_y = cur_y - scroll_y;
    if (view_x < 0 || view_x >= view_cols) return;
    if (view_y < 0 || view_y >= view_rows) return;
    char ch = cells[cur_y * cols + cur_x].ch;
    u8 inv = invert_attr(cur_attr);
    u32 px = text_x0 + (u32)view_x * (u32)cell_w;
    u32 py = text_y0 + (u32)view_y * (u32)cell_h;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, inv);
    if (ch != ' ') fb_draw_char_px_scaled(px, py, editor_visible_char(ch), inv, editor_zoom);
    caret_x = cur_x; caret_y = cur_y; caret_drawn = 1;
}

static void editor_clear_row(int y) {
    if (!cells || y < 0 || y >= rows) return;
    for (int x = 0; x < cols; x++) cells[y * cols + x].ch = ' ';
}

static void editor_shift_rows_down_from(int start_y) {
    // Shift rows [start_y .. rows-2] down by one, dropping last row
    if (!cells) return;
    if (start_y < 0) start_y = 0;
    if (start_y > rows - 1) return;
    for (int y = rows - 1; y > start_y; y--) {
        for (int x = 0; x < cols; x++) {
            cells[y * cols + x] = cells[(y - 1) * cols + x];
        }
    }
}

static void editor_shift_rows_up_from(int start_y) {
    // Shift rows [start_y+1 .. rows-1] up into [start_y .. rows-2]
    if (!cells) return;
    if (start_y < 0) start_y = 0;
    if (start_y >= rows - 1) return;
    for (int y = start_y; y < rows - 1; y++) {
        for (int x = 0; x < cols; x++) {
            cells[y * cols + x] = cells[(y + 1) * cols + x];
        }
    }
    // Clear last row
    editor_clear_row(rows - 1);
}

static inline editor_cell_t* at(int x, int y) {
    return &cells[y * cols + x];
}

static int editor_pos_after(int ax, int ay, int bx, int by) {
    return ay > by || (ay == by && ax > bx);
}

static void editor_clamp_eof(void) {
    if (doc_end_y < 0) doc_end_y = 0;
    if (doc_end_y >= rows) doc_end_y = rows - 1;
    if (doc_end_x < 0) doc_end_x = 0;
    if (doc_end_x > cols) doc_end_x = cols;
}

static void editor_extend_eof_to_cursor(void) {
    if (editor_pos_after(cur_x, cur_y, doc_end_x, doc_end_y)) {
        doc_end_x = cur_x;
        doc_end_y = cur_y;
        editor_clamp_eof();
    }
}

static void editor_clear_all(void) {
    if (!cells) return;
    for (int i = 0; i < rows * cols; i++) cells[i].ch = ' ';
    fb_fill_rect_attr(e_x, text_y0, e_w, e_h, cur_attr);
    cur_x = 0; cur_y = 0;
    doc_end_x = 0; doc_end_y = 0;
    scroll_x = 0; scroll_y = 0;
    caret_drawn = 0; editor_caret_draw();
}

static int editor_ensure_cursor_visible(void) {
    int old_scroll_x = scroll_x;
    int old_scroll_y = scroll_y;
    if (view_cols <= 0) view_cols = 1;
    if (view_rows <= 0) view_rows = 1;
    if (cur_x < scroll_x) scroll_x = cur_x;
    if (cur_x >= scroll_x + view_cols) scroll_x = cur_x - view_cols + 1;
    if (cur_y < scroll_y) scroll_y = cur_y;
    if (cur_y >= scroll_y + view_rows) scroll_y = cur_y - view_rows + 1;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_x > cols - view_cols) scroll_x = cols - view_cols;
    if (scroll_y > rows - view_rows) scroll_y = rows - view_rows;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_y < 0) scroll_y = 0;
    return scroll_x != old_scroll_x || scroll_y != old_scroll_y;
}

static void editor_rebuild_grid_for_zoom(int new_zoom) {
    if (new_zoom < 1) new_zoom = 1;
    if (new_zoom > 4) new_zoom = 4;

    int new_cell_w = base_cell_w * new_zoom;
    int new_cell_h = base_cell_h * new_zoom;
    if (new_cell_w <= 0) new_cell_w = 8;
    if (new_cell_h <= 0) new_cell_h = 16;

    u32 gutter_w = (u32)(EDITOR_LINE_NUMBER_COLS * new_cell_w);
    u32 text_w = (e_w > gutter_w) ? (e_w - gutter_w) : (u32)new_cell_w;
    int new_view_cols = (int)(text_w / (u32)new_cell_w);
    int new_view_rows = (int)(e_h / (u32)new_cell_h);
    if (new_view_cols <= 0) new_view_cols = 1;
    if (new_view_rows <= 0) new_view_rows = 1;
    int new_cols = EDITOR_MAX_COLS;
    int new_rows = EDITOR_MAX_ROWS;

    editor_cell_t* old_cells = cells;
    int old_cols = cols;
    int old_rows = rows;

    editor_cell_t* new_cells = (editor_cell_t*)kmalloc((size_t)(new_rows * new_cols * (int)sizeof(editor_cell_t)));
    if (!new_cells) return;
    for (int i = 0; i < new_rows * new_cols; i++) new_cells[i].ch = ' ';

    if (old_cells) {
        int copy_rows = old_rows < new_rows ? old_rows : new_rows;
        int copy_cols = old_cols < new_cols ? old_cols : new_cols;
        for (int y = 0; y < copy_rows; y++) {
            for (int x = 0; x < copy_cols; x++) {
                new_cells[y * new_cols + x] = old_cells[y * old_cols + x];
            }
        }
        kfree(old_cells);
    }

    cells = new_cells;
    editor_zoom = new_zoom;
    cell_w = new_cell_w;
    cell_h = new_cell_h;
    text_x0 = e_x + gutter_w;
    cols = new_cols;
    rows = new_rows;
    view_cols = new_view_cols;
    view_rows = new_view_rows;
    if (cur_x >= cols) cur_x = cols - 1;
    if (cur_y >= rows) cur_y = rows - 1;
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    editor_clamp_eof();
    caret_drawn = 0;
    (void)editor_ensure_cursor_visible();
    fb_fill_rect_attr(e_x, text_y0, e_w, e_h, cur_attr);
    editor_render_all();
}

static void editor_render_all(void) {
    if (!cells) return;
    fb_fill_rect_attr(e_x, text_y0, e_w, e_h, cur_attr);
    for (int view_y = 0; view_y < view_rows; view_y++) {
        int y = scroll_y + view_y;
        if (y < 0 || y >= rows) break;
        u32 py = text_y0 + (u32)view_y * (u32)cell_h;
        u32 gutter_w = (text_x0 > e_x) ? (text_x0 - e_x) : 0;
        if (gutter_w) {
            fb_fill_rect_attr(e_x, py, gutter_w, (u32)cell_h, 0x08);
            int line_no = y + 1;
            char digits[EDITOR_LINE_NUMBER_COLS + 1];
            for (int i = 0; i < EDITOR_LINE_NUMBER_COLS; i++) digits[i] = ' ';
            digits[EDITOR_LINE_NUMBER_COLS] = '\0';
            int pos = EDITOR_LINE_NUMBER_COLS - 2;
            while (line_no > 0 && pos >= 0) {
                digits[pos--] = (char)('0' + (line_no % 10));
                line_no /= 10;
            }
            for (int i = 0; i < EDITOR_LINE_NUMBER_COLS - 1; i++) {
                if (digits[i] != ' ') {
                    fb_draw_char_px_scaled(e_x + (u32)i * (u32)cell_w, py, digits[i], 0x08, editor_zoom);
                }
            }
        }
        for (int view_x = 0; view_x < view_cols; view_x++) {
            int x = scroll_x + view_x;
            if (x < 0 || x >= cols) break;
            char ch = cells[y * cols + x].ch;
            u8 attr = editor_lua_attr_at(x, y);
            u32 px = text_x0 + (u32)view_x * (u32)cell_w;
            // Draw background cell area, then glyph if not space
            fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, cur_attr);
            if (ch != ' ') fb_draw_char_px_scaled(px, py, editor_visible_char(ch), attr, editor_zoom);
        }
    }
    editor_caret_draw();
}

static void editor_draw_overlays(void) {
    // Draw titlebar File button
    const char* label = "File";
    u32 tw = text_width_px(label) + 12;
    u32 bx = tb_x + EDITOR_TITLE_BTN_PAD; u32 by = tb_y + 2; u32 bh = (tb_h > 4) ? (tb_h - 4) : tb_h;
    if (tw + 2 * EDITOR_TITLE_BTN_PAD > tb_w) tw = (tb_w > 2 * EDITOR_TITLE_BTN_PAD) ? (tb_w - 2 * EDITOR_TITLE_BTN_PAD) : 0;
    // Slight contrast button
    u8 btn_col[3] = { 0x40, 0x40, 0x44 };
    if (tw) fb_fill_rect_rgb(bx, by, tw, bh, btn_col[0], btn_col[1], btn_col[2]);
    draw_text(bx + 6, tb_y + 3, label, 0x0F);
    file_btn_x = bx; file_btn_y = by; file_btn_w = tw; file_btn_h = bh;

    // Dropdown under titlebar
    if (menu_open) {
        const char* i1 = "Open...";
        const char* i2 = "New...";
        const char* i3 = "Save";
        u32 w1 = text_width_px(i1) + 12;
        u32 w2 = text_width_px(i2) + 12;
        u32 w3 = text_width_px(i3) + 12;
        u32 mw = w1; if (w2 > mw) mw = w2; if (w3 > mw) mw = w3;
        u32 mx = file_btn_x; u32 my = tb_y + tb_h; // directly under titlebar
        u32 ih = 18;
        u8 sep[3] = { 0x40, 0x40, 0x44 };
        fb_fill_rect_rgb(mx, my, mw, ih * 3, sep[0], sep[1], sep[2]);
        draw_text(mx + 6, my + 2, i1, 0x0F);
        draw_text(mx + 6, my + ih + 2, i2, 0x0F);
        draw_text(mx + 6, my + ih * 2 + 2, i3, 0x0F);
        mi_open_x = mx; mi_open_y = my; mi_open_w = mw; mi_open_h = ih;
        mi_new_x  = mx; mi_new_y  = my + ih; mi_new_w = mw; mi_new_h = ih;
        mi_save_x = mx; mi_save_y = my + ih * 2; mi_save_w = mw; mi_save_h = ih;
        menu_x = mx; menu_y = my; menu_w = mw; menu_h = ih * 3;
    } else {
        mi_open_x = mi_open_y = mi_open_w = mi_open_h = 0;
        mi_new_x  = mi_new_y  = mi_new_w  = mi_new_h  = 0;
        mi_save_x = mi_save_y = mi_save_w = mi_save_h = 0;
        menu_x = menu_y = menu_w = menu_h = 0;
    }
}

static void editor_draw_modal(void) {
    if (modal_state == MODAL_NONE) return;
    u8 bg[3] = { 0x33, 0x33, 0x36 };
    u32 mw = 360, mh = 40;
    u32 cx = e_x + (e_w > mw ? (e_w - mw) / 2 : 0);
    u32 cy = e_y + (e_h > mh ? (e_h - mh) / 2 : 0);
    fb_fill_rect_rgb(cx, cy, mw, mh, bg[0], bg[1], bg[2]);
    // Border
    fb_fill_rect_rgb(cx, cy, mw, 1, 0x77,0x77,0x77);
    fb_fill_rect_rgb(cx, cy + mh - 1, mw, 1, 0x77,0x77,0x77);
    fb_fill_rect_rgb(cx, cy, 1, mh, 0x77,0x77,0x77);
    fb_fill_rect_rgb(cx + mw - 1, cy, 1, mh, 0x77,0x77,0x77);
    const char* title = (modal_state == MODAL_OPEN) ? "Open path:" : (modal_state == MODAL_NEW ? "New path:" : "Save path:");
    draw_text(cx + 8, cy + 6, title, 0x0F);
    draw_text(cx + 8, cy + 20, modal_buf, 0x0F);
}

void gui_editor_init(u32 px, u32 py, u32 pw, u32 ph) {
    e_x = px; e_y = py; e_w = pw; e_h = ph;
    int ok = fb_font_get_cell_size(&base_cell_w, &base_cell_h);
    if (!ok || base_cell_w <= 0 || base_cell_h <= 0) { base_cell_w = 8; base_cell_h = 16; }
    text_y0 = e_y;
    text_x0 = e_x + (u32)(EDITOR_LINE_NUMBER_COLS * base_cell_w * editor_zoom);
    cur_x = 0; cur_y = 0; cur_attr = 0x07;
    menu_open = 0; modal_state = MODAL_NONE; modal_len = 0; modal_buf[0] = '\0'; current_path = 0;
    cells = 0;
    editor_rebuild_grid_for_zoom(editor_zoom);
    if (!cells) return;
    editor_clear_all();
    editor_render_all();
}

void gui_editor_move(u32 px, u32 py) {
    e_x = px; e_y = py;
    text_y0 = e_y;
    text_x0 = e_x + (u32)(EDITOR_LINE_NUMBER_COLS * cell_w);
}

void gui_editor_render_all(void) { editor_render_all(); }

static void editor_put_char(char c) {
    if (!cells) return;
    editor_caret_undraw();
    int old_end_x = doc_end_x;
    int old_end_y = doc_end_y;
    int insert_x = cur_x;
    int insert_y = cur_y;
    if (c == '\n') {
        // Split line at cursor: move right part to a newly inserted next line
        if (cur_y < rows) {
            int next_y = cur_y + 1;
            // Make room for new line below (drop last)
            if (next_y < rows) {
                editor_shift_rows_down_from(next_y);
                editor_clear_row(next_y);
            }
            // Move right part (up to real end) to next line
            int end_x = editor_line_end_x(cur_y);
            if (end_x > cur_x) {
                int right_len = end_x - cur_x;
                if (next_y < rows) {
                    for (int i = 0; i < right_len && i < cols; i++) {
                        cells[next_y * cols + i].ch = cells[cur_y * cols + cur_x + i].ch;
                    }
                }
                // Clear right part on current line
                for (int x = cur_x; x < cols; x++) cells[cur_y * cols + x].ch = ' ';
            } else {
                // Nothing to move; next line is already cleared
            }
            if (next_y < rows) { cur_y = next_y; cur_x = 0; }
            if (insert_y < old_end_y) {
                doc_end_y = old_end_y + 1;
                doc_end_x = old_end_x;
            } else if (insert_y == old_end_y && insert_x <= old_end_x) {
                doc_end_y = next_y;
                doc_end_x = old_end_x - insert_x;
                if (doc_end_x < 0) doc_end_x = 0;
            } else {
                editor_extend_eof_to_cursor();
            }
            editor_clamp_eof();
            (void)editor_ensure_cursor_visible();
            editor_render_all();
            editor_caret_draw();
            return;
        }
    }
    // Place char and advance
    if (cur_x >= cols) { cur_x = 0; cur_y++; }
    if (cur_y >= rows) { cur_y = rows - 1; }
    insert_x = cur_x;
    insert_y = cur_y;
    for (int x = cols - 1; x > cur_x; x--) {
        cells[cur_y * cols + x].ch = cells[cur_y * cols + x - 1].ch;
    }
    at(cur_x, cur_y)->ch = c;
    cur_x++;
    if (cur_x >= cols) { cur_x = 0; cur_y++; if (cur_y >= rows) { cur_y = rows - 1; cur_x = cols - 1; } }
    if (insert_y < old_end_y || (insert_y == old_end_y && insert_x <= old_end_x)) {
        if (old_end_y == insert_y && old_end_x < cols) {
            doc_end_x = old_end_x + 1;
            doc_end_y = old_end_y;
        } else {
            doc_end_x = old_end_x;
            doc_end_y = old_end_y;
        }
    } else {
        editor_extend_eof_to_cursor();
    }
    editor_clamp_eof();
    (void)editor_ensure_cursor_visible();
    editor_render_all();
}

static int editor_line_end_x(int y) {
    if (y < 0 || y >= rows) return 0;
    int last = -1;
    for (int x = 0; x < cols; x++) {
        if (cells[y * cols + x].ch != ' ') last = x;
    }
    if (last < 0) return 0;           // empty line → start
    return last + 1;                  // position after last char (may equal cols when full)
}

static void editor_backspace(void) {
    if (!cells) return;
    editor_caret_undraw();
    int old_end_x = doc_end_x;
    int old_end_y = doc_end_y;
    if (cur_x == 0) {
        if (cur_y == 0) { editor_caret_draw(); return; }
        // Join current line into previous by removing the newline
        int prev_y = cur_y - 1;
        int prev_len = editor_line_end_x(prev_y);
        if (prev_len > cols) prev_len = cols;
        int curr_len = editor_line_end_x(cur_y);
        if (curr_len > cols) curr_len = cols;
        int space = cols - prev_len;
        if (space < 0) space = 0;
        int copy_len = curr_len < space ? curr_len : space;
        // Copy portion of current line to end of previous line
        for (int i = 0; i < copy_len; i++) {
            cells[prev_y * cols + prev_len + i].ch = cells[cur_y * cols + i].ch;
        }
        // Shift current line left by copy_len
        if (copy_len > 0) {
            for (int x = 0; x < cols - copy_len; x++) {
                cells[cur_y * cols + x].ch = cells[cur_y * cols + x + copy_len].ch;
            }
            for (int x = cols - copy_len; x < cols; x++) cells[cur_y * cols + x].ch = ' ';
        }
        if (copy_len == curr_len) {
            // Entire current line merged into previous; remove current line by shifting rows up
            editor_shift_rows_up_from(cur_y);
            cur_y = prev_y;
            cur_x = prev_len + copy_len;
        } else {
            // Partial merge; keep current line (newline remains), move cursor to end of appended part
            cur_y = prev_y;
            cur_x = prev_len + copy_len;
        }
        if (old_end_y > cur_y + 1) {
            doc_end_y = old_end_y - 1;
            doc_end_x = old_end_x;
        } else if (old_end_y == cur_y + 1) {
            doc_end_y = cur_y;
            doc_end_x = prev_len + old_end_x;
            if (doc_end_x > cols) doc_end_x = cols;
        }
        editor_clamp_eof();
        (void)editor_ensure_cursor_visible();
        editor_render_all();
        editor_caret_draw();
        return;
    } else {
        cur_x--;
        int delete_x = cur_x;
        int delete_y = cur_y;
        for (int x = cur_x; x < cols - 1; x++) {
            cells[cur_y * cols + x].ch = cells[cur_y * cols + x + 1].ch;
        }
        cells[cur_y * cols + cols - 1].ch = ' ';
        if (delete_y == old_end_y && delete_x < old_end_x) {
            doc_end_x = old_end_x - 1;
            doc_end_y = old_end_y;
        }
        editor_clamp_eof();
        (void)editor_ensure_cursor_visible();
        editor_render_all();
    }
}

static int editor_move_cursor(int dx, int dy) {
    int nx = cur_x + dx; int ny = cur_y + dy;
    if (nx < 0) {
        if (ny > 0) {
            ny--;
            nx = editor_line_end_x(ny);
            if (nx >= cols) nx = cols - 1;
        } else {
            nx = 0;
        }
    }
    if (nx >= cols) {
        if (ny < rows - 1) {
            nx = 0;
            ny++;
        } else {
            nx = cols - 1;
        }
    }
    if (ny < 0) ny = 0;
    if (ny >= rows) ny = rows - 1;
    cur_x = nx; cur_y = ny;
    return editor_ensure_cursor_visible();
}

static void editor_save_to_file(const char* path) {
    if (!cells) return;
    Node *f = cldramfs_resolve_path_file(path, 1);
    if (!f) return;
    u32 total = 0;
    for (int y = 0; y <= doc_end_y; y++) {
        int line_len = (y == doc_end_y) ? doc_end_x : editor_line_end_x(y);
        if (line_len < 0) line_len = 0;
        if (line_len > cols) line_len = cols;
        total += (u32)line_len;
        if (y < doc_end_y) total++;
    }
    char *buf = (char*)kmalloc(total + 1);
    if (!buf) return;
    char *p = buf;
    for (int y = 0; y <= doc_end_y; y++) {
        int line_len = (y == doc_end_y) ? doc_end_x : editor_line_end_x(y);
        if (line_len < 0) line_len = 0;
        if (line_len > cols) line_len = cols;
        for (int x = 0; x < line_len; x++) {
            *p++ = cells[y * cols + x].ch;
        }
        if (y < doc_end_y) *p++ = '\n';
    }
    *p = '\0';
    if (f->content) kfree(f->content);
    f->content = buf;
    f->content_size = (u32)(p - buf);
    // Optional: brief visual flash of top-left cell to indicate save
    u32 px = text_x0; u32 py = e_y;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, 0x20); // green on black
    fb_draw_char_px_scaled(px, py, 'S', 0x20, editor_zoom);
}

void gui_editor_handle_key(u8 scancode, int is_extended, int is_pressed) {
    if (!is_pressed) return;
    u128 ka = ps2_keyarr();
    int shift = (ka & ((u128)1 << 0x2A)) || (ka & ((u128)1 << 0x36));
    int ctrl  = (ka & ((u128)1 << 0x1D)) != 0; // Ctrl key

    // Modal input handling
    if (modal_state != MODAL_NONE) {
        if (is_extended) return; // ignore arrows while modal
        switch (scancode) {
            case US_ESC:
                modal_state = MODAL_NONE; modal_len = 0; modal_buf[0] = '\0';
                gui_editor_render_all(); gui_editor_draw_overlays();
                return;
            case US_ENTER: {
                modal_buf[modal_len] = '\0';
                if (modal_state == MODAL_OPEN) {
                    Node* f = cldramfs_resolve_path_file(modal_buf, 0);
                    if (f && f->type == FILE_NODE && f->content) {
                        // Load file content into grid (truncate/pad)
                        editor_clear_all();
                        const char* p = f->content; u32 i = 0; u32 sz = f->content_size;
                        int x = 0;
                        int y = 0;
                        doc_end_x = 0;
                        doc_end_y = 0;
                        while (i < sz && y < rows) {
                            char ch = p[i++];
                            if (ch == '\r') continue;
                            if (ch == '\n') {
                                doc_end_x = 0;
                                doc_end_y = y + 1;
                                y++;
                                x = 0;
                                continue;
                            }
                            if (x < cols) {
                                cells[y * cols + x].ch = (ch ? ch : ' ');
                                x++;
                                doc_end_x = x;
                                doc_end_y = y;
                            }
                        }
                        editor_clamp_eof();
                        // Set current path
                        if (current_path) { kfree(current_path); current_path = 0; }
                        size_t n = strlen(modal_buf); current_path = (char*)kmalloc(n + 1); if (current_path) strcpy(current_path, modal_buf);
                    }
                } else if (modal_state == MODAL_NEW) {
                    if (current_path) { kfree(current_path); current_path = 0; }
                    size_t n = strlen(modal_buf); current_path = (char*)kmalloc(n + 1); if (current_path) strcpy(current_path, modal_buf);
                    (void)cldramfs_resolve_path_file(modal_buf, 1);
                    editor_clear_all();
                } else if (modal_state == MODAL_SAVE) {
                    if (modal_len > 0) {
                        if (current_path) { kfree(current_path); current_path = 0; }
                        size_t n = strlen(modal_buf); current_path = (char*)kmalloc(n + 1); if (current_path) strcpy(current_path, modal_buf);
                        editor_save_to_file(current_path);
                    }
                }
                modal_state = MODAL_NONE; modal_len = 0; modal_buf[0] = '\0';
                gui_editor_render_all(); gui_editor_draw_overlays();
                return;
            }
            case US_BACKSPACE:
                if (modal_len > 0) { modal_len--; modal_buf[modal_len] = '\0'; gui_editor_render_all(); gui_editor_draw_overlays(); }
                return;
            default: {
                if (is_printable_key(scancode) && modal_len < (int)sizeof(modal_buf) - 1) {
                    char c = scancode_to_char(scancode, shift);
                    if (c) { modal_buf[modal_len++] = c; modal_buf[modal_len] = '\0'; gui_editor_render_all(); gui_editor_draw_overlays(); }
                }
                return;
            }
        }
    }

    if (is_extended) {
        switch (scancode) {
            case US_ARROW_LEFT:
                editor_caret_undraw();
                if (ctrl && scroll_x > 0) { scroll_x--; editor_render_all(); }
                else if (editor_move_cursor(-1, 0)) editor_render_all();
                else editor_caret_draw();
                return;
            case US_ARROW_RIGHT:
                editor_caret_undraw();
                if (ctrl && scroll_x < cols - view_cols) { scroll_x++; editor_render_all(); }
                else if (editor_move_cursor(1, 0)) editor_render_all();
                else editor_caret_draw();
                return;
            case US_ARROW_UP:
                editor_caret_undraw();
                if (ctrl && scroll_y > 0) { scroll_y--; editor_render_all(); }
                else if (editor_move_cursor(0, -1)) editor_render_all();
                else editor_caret_draw();
                return;
            case US_ARROW_DOWN:
                editor_caret_undraw();
                if (ctrl && scroll_y < rows - view_rows) { scroll_y++; editor_render_all(); }
                else if (editor_move_cursor(0, 1)) editor_render_all();
                else editor_caret_draw();
                return;
            default: return;
        }
    }

    if (ctrl && scancode == US_MINUS) {
        editor_rebuild_grid_for_zoom(editor_zoom - 1);
        return;
    }
    if (ctrl && scancode == US_EQUAL) {
        editor_rebuild_grid_for_zoom(editor_zoom + 1);
        return;
    }

    // Shortcuts
    if (ctrl && scancode == US_S) {
        editor_save_to_file("/edit.txt");
        return;
    }

    switch (scancode) {
        case US_ENTER:      editor_put_char('\n'); return;
        case US_BACKSPACE:  editor_backspace();    return;
        case US_TAB:        editor_put_char('\t'); return;
        default: break;
    }
    if (is_printable_key(scancode)) {
        char c = scancode_to_char(scancode, shift);
        if (c) editor_put_char(c);
    }
}

void gui_editor_free(void) {
    if (cells) { kfree(cells); cells = 0; }
    if (current_path) { kfree(current_path); current_path = 0; }
}

static int point_in(u32 x, u32 y, u32 rx, u32 ry, u32 rw, u32 rh) {
    return (rw && rh && x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

int gui_editor_on_click(u32 px, u32 py) {
    int changed = 0;
    // Toggle File menu
    if (point_in(px, py, file_btn_x, file_btn_y, file_btn_w, file_btn_h)) {
        menu_open = !menu_open; changed = 1;
        return changed;
    }
    if (menu_open) {
        if (point_in(px, py, mi_open_x, mi_open_y, mi_open_w, mi_open_h)) {
            menu_open = 0; modal_state = MODAL_OPEN; modal_len = 0; modal_buf[0] = '\0'; changed = 1;
        } else if (point_in(px, py, mi_new_x, mi_new_y, mi_new_w, mi_new_h)) {
            menu_open = 0; modal_state = MODAL_NEW; modal_len = 0; modal_buf[0] = '\0'; changed = 1;
        } else if (point_in(px, py, mi_save_x, mi_save_y, mi_save_w, mi_save_h)) {
            menu_open = 0; changed = 1;
            if (current_path) editor_save_to_file(current_path);
            else editor_save_to_file("/edit.txt");
        } else if (!point_in(px, py, menu_x, menu_y, menu_w, menu_h) && !point_in(px, py, file_btn_x, file_btn_y, file_btn_w, file_btn_h)) {
            // click outside menu closes it
            menu_open = 0; changed = 1;
        }
        return changed;
    }
    return 0;
}

int gui_editor_on_move(u32 px, u32 py) { (void)px; (void)py; return 0; }

void gui_editor_set_titlebar(u32 win_x, u32 win_y, u32 win_w, u32 title_h) {
    tb_x = win_x + 2; tb_y = win_y + 2; tb_w = win_w - 4; tb_h = title_h;
}

void gui_editor_draw_overlays(void) { editor_draw_overlays(); editor_draw_modal(); }
