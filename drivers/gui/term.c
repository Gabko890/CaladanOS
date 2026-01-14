#include "term.h"
#include <fb/fb_console.h>
#include <vgaio.h>

// Terminal state
static u32 t_x = 0, t_y = 0, t_w = 0, t_h = 0; // pixel rect
static int cell_w = 8, cell_h = 16;
static int cols = 0, rows = 0;
static int cur_x = 0, cur_y = 0;
static u8 cur_attr = 0x07; // white on black by default

// Minimal ANSI parser (mirrors vgaio subset)
typedef enum {
    T_ANSI_NORMAL,
    T_ANSI_ESC,
    T_ANSI_CSI
} t_ansi_state_t;
static t_ansi_state_t t_state = T_ANSI_NORMAL;
static char t_buf[16];
static int t_buf_pos = 0;

static void term_clear_all(void) {
    fb_fill_rect_attr(t_x, t_y, t_w, t_h, cur_attr);
    cur_x = 0; cur_y = 0;
}

static void term_clear_line_full(void) {
    u32 py = t_y + (u32)cur_y * (u32)cell_h;
    fb_fill_rect_attr(t_x, py, t_w, (u32)cell_h, cur_attr);
}

static void term_clear_to_eol(void) {
    u32 px = t_x + (u32)cur_x * (u32)cell_w;
    u32 pw = t_w - (u32)cur_x * (u32)cell_w;
    u32 py = t_y + (u32)cur_y * (u32)cell_h;
    fb_fill_rect_attr(px, py, pw, (u32)cell_h, cur_attr);
}

static void term_scroll_up(void) {
    fb_scroll_region_up(t_x, t_y, t_w, t_h, (u32)cell_h, cur_attr);
}

static void term_putc(char c) {
    if (c == '\n') {
        cur_x = 0; cur_y++;
    } else {
        if (cur_y >= rows) { cur_y = rows - 1; term_scroll_up(); }
        if (cur_x >= cols) { cur_x = 0; cur_y++; if (cur_y >= rows) { cur_y = rows - 1; term_scroll_up(); } }
        u32 px = t_x + (u32)cur_x * (u32)cell_w;
        u32 py = t_y + (u32)cur_y * (u32)cell_h;
        fb_draw_char_px(px, py, c, cur_attr);
        cur_x++;
        if (cur_x >= cols) { cur_x = 0; cur_y++; }
    }
    if (cur_y >= rows) { cur_y = rows - 1; term_scroll_up(); }
}

static void term_handle_ansi(void) {
    t_buf[t_buf_pos] = '\0';
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
    term_clear_all();
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
