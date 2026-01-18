#include "editor.h"
#include <fb/fb_console.h>
#include <kmalloc.h>
#include <ps2.h>
#include <string.h>
#include <cldramfs/cldramfs.h>
#include <cldramfs/tty.h>

// Very simple text editor rendered in a pixel rect using framebuffer font

static u32 e_x = 0, e_y = 0, e_w = 0, e_h = 0; // pixel rect
static int cell_w = 8, cell_h = 16;
static int cols = 0, rows = 0;
static int cur_x = 0, cur_y = 0;
static u8 cur_attr = 0x07; // white on black

typedef struct { char ch; } editor_cell_t;
static editor_cell_t* cells = 0; // rows*cols
static int caret_drawn = 0;
static int caret_x = 0, caret_y = 0;

// Forward declarations for helpers used before their definitions
static int editor_line_end_x(int y);

static inline u8 invert_attr(u8 a) { return (u8)(((a & 0x0F) << 4) | ((a >> 4) & 0x0F)); }

static void editor_draw_cell(int x, int y) {
    if (!cells) return;
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    char ch = cells[y * cols + x].ch;
    u32 px = e_x + (u32)x * (u32)cell_w;
    u32 py = e_y + (u32)y * (u32)cell_h;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, cur_attr);
    if (ch != ' ') fb_draw_char_px(px, py, ch, cur_attr);
}

static void editor_caret_undraw(void) {
    if (!caret_drawn) return;
    editor_draw_cell(caret_x, caret_y);
    caret_drawn = 0;
}

static void editor_caret_draw(void) {
    if (!cells) return;
    if (cur_x < 0 || cur_y < 0 || cur_x >= cols || cur_y >= rows) return;
    char ch = cells[cur_y * cols + cur_x].ch;
    u8 inv = invert_attr(cur_attr);
    u32 px = e_x + (u32)cur_x * (u32)cell_w;
    u32 py = e_y + (u32)cur_y * (u32)cell_h;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, inv);
    if (ch != ' ') fb_draw_char_px(px, py, ch, inv);
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

static void editor_clear_all(void) {
    if (!cells) return;
    for (int i = 0; i < rows * cols; i++) cells[i].ch = ' ';
    fb_fill_rect_attr(e_x, e_y, e_w, e_h, cur_attr);
    cur_x = 0; cur_y = 0;
    caret_drawn = 0; editor_caret_draw();
}

static void editor_render_all(void) {
    if (!cells) return;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            char ch = cells[y * cols + x].ch;
            u32 px = e_x + (u32)x * (u32)cell_w;
            u32 py = e_y + (u32)y * (u32)cell_h;
            // Draw background cell area, then glyph if not space
            fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, cur_attr);
            if (ch != ' ') fb_draw_char_px(px, py, ch, cur_attr);
        }
    }
    editor_caret_draw();
}

void gui_editor_init(u32 px, u32 py, u32 pw, u32 ph) {
    e_x = px; e_y = py; e_w = pw; e_h = ph;
    int ok = fb_font_get_cell_size(&cell_w, &cell_h);
    if (!ok || cell_w <= 0 || cell_h <= 0) { cell_w = 8; cell_h = 16; }
    cols = (int)(pw / (u32)cell_w);
    rows = (int)(ph / (u32)cell_h);
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;
    cur_x = 0; cur_y = 0; cur_attr = 0x07;
    // Allocate backing store
    cells = (editor_cell_t*)kmalloc((size_t)(rows * cols * (int)sizeof(editor_cell_t)));
    if (!cells) return;
    editor_clear_all();
}

void gui_editor_move(u32 px, u32 py) {
    e_x = px; e_y = py;
}

void gui_editor_render_all(void) { editor_render_all(); }

static void editor_scroll_up(void) {
    if (!cells || rows <= 1) return;
    // Move rows up
    for (int y = 0; y < rows - 1; y++) {
        for (int x = 0; x < cols; x++) {
            cells[y * cols + x] = cells[(y + 1) * cols + x];
        }
    }
    // Clear last row
    for (int x = 0; x < cols; x++) cells[(rows - 1) * cols + x].ch = ' ';
    editor_render_all();
}

static void editor_put_char(char c) {
    if (!cells) return;
    editor_caret_undraw();
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
            editor_render_all();
            editor_caret_draw();
            return;
        }
    }
    // Place char and advance
    if (cur_x >= cols) { cur_x = 0; cur_y++; }
    if (cur_y >= rows) { cur_y = rows - 1; editor_scroll_up(); }
    at(cur_x, cur_y)->ch = c;
    u32 px = e_x + (u32)cur_x * (u32)cell_w;
    u32 py = e_y + (u32)cur_y * (u32)cell_h;
    fb_draw_char_px(px, py, c, cur_attr);
    cur_x++;
    if (cur_x >= cols) { cur_x = 0; cur_y++; if (cur_y >= rows) { cur_y = rows - 1; editor_scroll_up(); } }
    editor_caret_draw();
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
        editor_render_all();
        editor_caret_draw();
        return;
    } else {
        cur_x--;
        at(cur_x, cur_y)->ch = ' ';
        u32 px = e_x + (u32)cur_x * (u32)cell_w;
        u32 py = e_y + (u32)cur_y * (u32)cell_h;
        fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, cur_attr);
        editor_caret_draw();
    }
}

static void editor_move_cursor(int dx, int dy) {
    int nx = cur_x + dx; int ny = cur_y + dy;
    if (nx < 0) { if (ny > 0) { ny--; nx = cols - 1; } else nx = 0; }
    if (nx >= cols) { nx = 0; ny++; }
    if (ny < 0) ny = 0;
    if (ny >= rows) ny = rows - 1;
    cur_x = nx; cur_y = ny;
}

static void editor_save_to_file(const char* path) {
    if (!cells) return;
    Node *f = cldramfs_resolve_path_file(path, 1);
    if (!f) return;
    // Build a simple text with newline per row
    u32 line_len = (u32)cols;
    u32 total = (u32)rows * (line_len + 1); // +\n per line
    char *buf = (char*)kmalloc(total + 1);
    if (!buf) return;
    char *p = buf;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            *p++ = cells[y * cols + x].ch;
        }
        *p++ = '\n';
    }
    *p = '\0';
    if (f->content) kfree(f->content);
    f->content = buf;
    f->content_size = (u32)(p - buf);
    // Optional: brief visual flash of top-left cell to indicate save
    u32 px = e_x; u32 py = e_y;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, 0x20); // green on black
    fb_draw_char_px(px, py, 'S', 0x20);
}

void gui_editor_handle_key(u8 scancode, int is_extended, int is_pressed) {
    if (!is_pressed) return;
    u128 ka = ps2_keyarr();
    int shift = (ka & ((u128)1 << 0x2A)) || (ka & ((u128)1 << 0x36));
    int ctrl  = (ka & ((u128)1 << 0x1D)) != 0; // Ctrl key

    if (is_extended) {
        switch (scancode) {
            case US_ARROW_LEFT:  editor_caret_undraw(); editor_move_cursor(-1, 0); editor_caret_draw(); return;
            case US_ARROW_RIGHT: editor_caret_undraw(); editor_move_cursor( 1, 0); editor_caret_draw(); return;
            case US_ARROW_UP:    editor_caret_undraw(); editor_move_cursor( 0,-1); editor_caret_draw(); return;
            case US_ARROW_DOWN:  editor_caret_undraw(); editor_move_cursor( 0, 1); editor_caret_draw(); return;
            default: return;
        }
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
}
