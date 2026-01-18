#include "term.h"
#include <fb/fb_console.h>
#include <vgaio.h>
#include <kmalloc.h>

// Terminal state
static u32 t_x = 0, t_y = 0, t_w = 0, t_h = 0; // pixel rect
static int cell_w = 8, cell_h = 16;
static int cols = 0, rows = 0;
static int cur_x = 0, cur_y = 0;
static u8 cur_attr = 0x07; // white on black by default

typedef struct { char ch; u8 attr; } TermCell;
static TermCell* cells = 0; // rows*cols
static int caret_drawn = 0;
static int caret_x = 0, caret_y = 0;

static inline u8 invert_attr(u8 a) {
    return (u8)(((a & 0x0F) << 4) | ((a >> 4) & 0x0F));
}

static void term_draw_cell(int x, int y) {
    if (!cells) return;
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    TermCell c = cells[y * cols + x];
    u32 px = t_x + (u32)x * (u32)cell_w;
    u32 py = t_y + (u32)y * (u32)cell_h;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, c.attr);
    if (c.ch != ' ') fb_draw_char_px(px, py, c.ch, c.attr);
}

static void term_caret_undraw(void) {
    if (!caret_drawn) return;
    term_draw_cell(caret_x, caret_y);
    caret_drawn = 0;
}

static void term_caret_draw(void) {
    if (!cells) return;
    if (cur_x < 0 || cur_y < 0 || cur_x >= cols || cur_y >= rows) return;
    TermCell c = cells[cur_y * cols + cur_x];
    u8 inv = invert_attr(c.attr);
    u32 px = t_x + (u32)cur_x * (u32)cell_w;
    u32 py = t_y + (u32)cur_y * (u32)cell_h;
    fb_fill_rect_attr(px, py, (u32)cell_w, (u32)cell_h, inv);
    if (c.ch != ' ') fb_draw_char_px(px, py, c.ch, inv);
    caret_x = cur_x; caret_y = cur_y; caret_drawn = 1;
}

// Minimal ANSI parser (mirrors vgaio subset)
typedef enum {
    T_ANSI_NORMAL,
    T_ANSI_ESC,
    T_ANSI_CSI
} t_ansi_state_t;
static t_ansi_state_t t_state = T_ANSI_NORMAL;
static char t_buf[16];
static int t_buf_pos = 0;

static void term_render_all(void) {
    if (!cells) return;
    // Draw background implicitly via glyph bg for each cell
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            TermCell c = cells[y * cols + x];
            u32 px = t_x + (u32)x * (u32)cell_w;
            u32 py = t_y + (u32)y * (u32)cell_h;
            fb_draw_char_px(px, py, c.ch, c.attr);
        }
    }
    term_caret_draw();
}

static void term_clear_all(void) {
    // Reset backing store to spaces
    if (cells) {
        for (int i = 0; i < rows * cols; i++) { cells[i].ch = ' '; cells[i].attr = cur_attr; }
    }
    fb_fill_rect_attr(t_x, t_y, t_w, t_h, cur_attr);
    cur_x = 0; cur_y = 0;
    caret_drawn = 0; term_caret_draw();
}

static void term_clear_line_full(void) {
    // Clear backing store row
    if (cells) {
        for (int x = 0; x < cols; x++) { cells[cur_y * cols + x].ch = ' '; cells[cur_y * cols + x].attr = cur_attr; }
    }
    u32 py = t_y + (u32)cur_y * (u32)cell_h;
    fb_fill_rect_attr(t_x, py, t_w, (u32)cell_h, cur_attr);
    caret_drawn = 0; term_caret_draw();
}

static void term_clear_to_eol(void) {
    if (cells) {
        for (int x = cur_x; x < cols; x++) { cells[cur_y * cols + x].ch = ' '; cells[cur_y * cols + x].attr = cur_attr; }
    }
    u32 px = t_x + (u32)cur_x * (u32)cell_w;
    u32 pw = t_w - (u32)cur_x * (u32)cell_w;
    u32 py = t_y + (u32)cur_y * (u32)cell_h;
    fb_fill_rect_attr(px, py, pw, (u32)cell_h, cur_attr);
    caret_drawn = 0; term_caret_draw();
}

static void term_scroll_up(void) {
    if (!cells) return;
    // Move rows up in backing store
    for (int y = 0; y < rows - 1; y++) {
        for (int x = 0; x < cols; x++) {
            cells[y * cols + x] = cells[(y + 1) * cols + x];
        }
    }
    // Clear last row
    for (int x = 0; x < cols; x++) { cells[(rows - 1) * cols + x].ch = ' '; cells[(rows - 1) * cols + x].attr = cur_attr; }
    // Redraw all (simple and robust)
    term_render_all();
}

static void term_putc(char c) {
    term_caret_undraw();
    if (c == '\n') {
        cur_x = 0; cur_y++;
    } else {
        if (cur_y >= rows) { cur_y = rows - 1; term_scroll_up(); }
        if (cur_x >= cols) { cur_x = 0; cur_y++; if (cur_y >= rows) { cur_y = rows - 1; term_scroll_up(); } }
        // Write to backing store and draw cell
        if (cells) {
            cells[cur_y * cols + cur_x].ch = c;
            cells[cur_y * cols + cur_x].attr = cur_attr;
        }
        u32 px = t_x + (u32)cur_x * (u32)cell_w;
        u32 py = t_y + (u32)cur_y * (u32)cell_h;
        fb_draw_char_px(px, py, c, cur_attr);
        cur_x++;
        if (cur_x >= cols) { cur_x = 0; cur_y++; }
    }
    if (cur_y >= rows) { cur_y = rows - 1; term_scroll_up(); }
    term_caret_draw();
}

static void term_handle_ansi(void) {
    t_buf[t_buf_pos] = '\0';
    term_caret_undraw();
    if (t_buf_pos == 1) {
        switch (t_buf[0]) {
            case 'C': // right
                if (cur_x < cols - 1) cur_x++;
                break;
            case 'D': // left
                if (cur_x > 0) cur_x--;
                break;
            case 'H': // home
                cur_x = 0; cur_y = 0;
                break;
            case 'K': // clear to EOL
                term_clear_to_eol();
                break;
            case 'G': // move to beginning of line
                cur_x = 0;
                break;
        }
    } else if (t_buf_pos == 2) {
        if (t_buf[0] == '2') {
            switch (t_buf[1]) {
                case 'J': // clear screen
                    term_clear_all();
                    break;
                case 'K': // clear line
                    term_clear_line_full();
                    break;
            }
        }
    }
    t_state = T_ANSI_NORMAL; t_buf_pos = 0;
    term_caret_draw();
}

void gui_term_init(u32 px, u32 py, u32 pw, u32 ph) {
    t_x = px; t_y = py; t_w = pw; t_h = ph;
    int ok = fb_font_get_cell_size(&cell_w, &cell_h);
    if (!ok || cell_w <= 0 || cell_h <= 0) { cell_w = 8; cell_h = 16; }
    cols = (int)(pw / (u32)cell_w);
    rows = (int)(ph / (u32)cell_h);
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;
    cur_x = 0; cur_y = 0; cur_attr = 0x07;
    // Allocate backing store
    cells = (TermCell*)kmalloc((size_t)(rows * cols * (int)sizeof(TermCell)));
    if (cells) {
        for (int i = 0; i < rows * cols; i++) { cells[i].ch = ' '; cells[i].attr = cur_attr; }
    }
    // Clear pixels to background
    fb_fill_rect_attr(t_x, t_y, t_w, t_h, cur_attr);
    caret_drawn = 0; term_caret_draw();
}

void gui_term_set_attr(u8 attr) {
    cur_attr = attr;
}

void gui_term_putchar(char c) {
    switch (t_state) {
        case T_ANSI_NORMAL:
            if (c == '\x1b') { t_state = T_ANSI_ESC; t_buf_pos = 0; return; }
            break;
        case T_ANSI_ESC:
            if (c == '[') { t_state = T_ANSI_CSI; t_buf_pos = 0; return; }
            else { t_state = T_ANSI_NORMAL; t_buf_pos = 0; }
            break;
        case T_ANSI_CSI:
            if ((c >= '0' && c <= '9') || c == ';') {
                if (t_buf_pos < (int)sizeof(t_buf) - 1) t_buf[t_buf_pos++] = c;
                return;
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                if (t_buf_pos < (int)sizeof(t_buf) - 1) t_buf[t_buf_pos++] = c;
                term_handle_ansi();
                return;
            } else { t_state = T_ANSI_NORMAL; t_buf_pos = 0; }
            break;
    }
    // Normal char
    term_putc(c);
}

void gui_term_attach(void) {
    vga_set_putchar_sink(gui_term_putchar, 1);
    vga_set_attr_sink(gui_term_set_attr, 1);
}

void gui_term_detach(void) {
    vga_clear_putchar_sink();
    vga_clear_attr_sink();
}

void gui_term_move(u32 px, u32 py) {
    t_x = px; t_y = py;
}

void gui_term_render_all(void) {
    term_render_all();
}

void gui_term_free(void) {
    if (cells) {
        kfree(cells);
        cells = 0;
    }
}
