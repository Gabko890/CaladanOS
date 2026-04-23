#include "window.h"
#include "bar.h"
#include <cldramfs/tty.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <string.h>

#define TITLE_H             24
#define BORDER_W            2
#define CONTENT_PAD_X       6
#define CONTENT_PAD_BOTTOM  6
#define BUTTON_SIZE         12
#define BUTTON_GAP          6
#define RESIZE_GRIP         14
#define MENU_H              18
#define DRAG_STEP_PIXELS    4

typedef enum {
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE
} drag_mode_t;

static gui_window_t windows[GUI_WINDOW_MAX];
static int z_order[GUI_WINDOW_MAX];
static int z_count = 0;
static int next_id = 1;
static int active_id = -1;
static drag_mode_t drag_mode = DRAG_NONE;
static gui_window_t *drag_win = 0;
static u32 drag_off_x = 0;
static u32 drag_off_y = 0;
static u32 drag_start_x = 0;
static u32 drag_start_y = 0;
static u32 drag_start_w = 0;
static u32 drag_start_h = 0;
static u32 drag_last_x = 0;
static u32 drag_last_y = 0;
static u32 drag_preview_x = 0;
static u32 drag_preview_y = 0;
static u32 drag_preview_w = 0;
static u32 drag_preview_h = 0;

static gui_window_style_t win_style = {
    { 0xCC, 0xCC, 0xCC },
    { 0x60, 0x60, 0x64 },
    { 0x48, 0x64, 0x7A },
    { 0x00, 0x00, 0x00 },
    { 0x40, 0x40, 0x44 },
    { 0x33, 0x33, 0x36 },
    { 0xCC, 0x33, 0x33 },
    { 0xCC, 0xAA, 0x33 },
    { 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00 },
};

static void copy_text(char *dst, u32 dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    u32 i = 0;
    if (src) {
        while (i + 1 < dst_len && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void draw_rect(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    if (!w || !h) return;
    fb_fill_rect_rgb(x, y, w, h, rgb[0], rgb[1], rgb[2]);
}

static void draw_border(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    if (w < 2 || h < 2) return;
    draw_rect(x, y, w, BORDER_W, rgb);
    draw_rect(x, y + h - BORDER_W, w, BORDER_W, rgb);
    draw_rect(x, y, BORDER_W, h, rgb);
    draw_rect(x + w - BORDER_W, y, BORDER_W, h, rgb);
}

static void draw_outline(u32 x, u32 y, u32 w, u32 h) {
    if (w < 4 || h < 4) return;
    draw_border(x, y, w, h, win_style.outline_light);
    draw_border(x + 1, y + 1, w - 2, h - 2, win_style.outline_dark);
}

static u32 text_width(const char *s) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0;
    while (s && s[n]) n++;
    return (u32)cw * n;
}

static void draw_text(u32 x, u32 y, const char *s, u8 attr) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u8 fg = attr & 0x0F;
    while (s && *s) {
        fb_draw_char_px_nobg(x, y, *s, fg);
        x += (u32)cw;
        s++;
    }
}

static int slot_of(gui_window_t *win) {
    if (!win) return -1;
    for (int i = 0; i < GUI_WINDOW_MAX; i++) {
        if (&windows[i] == win) return i;
    }
    return -1;
}

static int z_index_of_slot(int slot) {
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == slot) return i;
    }
    return -1;
}

static void remove_from_z_order(int slot) {
    int zi = z_index_of_slot(slot);
    if (zi < 0) return;
    for (int i = zi; i < z_count - 1; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_count--;
}

static void close_all_menus_except(gui_window_t *keep) {
    for (int i = 0; i < GUI_WINDOW_MAX; i++) {
        if (windows[i].used && &windows[i] != keep) windows[i].menu_open = 0;
    }
}

static int point_in(u32 x, u32 y, u32 rx, u32 ry, u32 rw, u32 rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int abs_i(int v) {
    return v < 0 ? -v : v;
}

static void close_button_rect(gui_window_t *win, u32 *x, u32 *y, u32 *w, u32 *h) {
    *w = BUTTON_SIZE;
    *h = BUTTON_SIZE;
    *x = win->x + win->w - BUTTON_GAP - *w;
    *y = win->y + 4;
}

static void minimize_button_rect(gui_window_t *win, u32 *x, u32 *y, u32 *w, u32 *h) {
    *w = BUTTON_SIZE;
    *h = BUTTON_SIZE;
    *x = win->x + win->w - BUTTON_GAP * 2 - BUTTON_SIZE * 2;
    *y = win->y + 4;
}

static u32 title_text_x(gui_window_t *win) {
    u32 x = win->x + 8;
    u32 inset = win->title_left_inset;
    if (win->menu_count > 0) {
        u32 menu_inset = text_width("File") + 18;
        if (menu_inset > inset) inset = menu_inset;
    }
    x += inset;
    return x;
}

static void draw_menu(gui_window_t *win) {
    if (win->menu_count <= 0) return;
    u32 bx = win->x + 8;
    u32 by = win->y + 3;
    u32 bw = text_width("File") + 12;
    u32 bh = TITLE_H - 6;
    if (win->menu_open) draw_rect(bx, by, bw, bh, win_style.menu);
    draw_text(bx + 6, win->y + 5, "File", 0x0F);

    if (!win->menu_open) return;

    u32 item_w = 0;
    for (int i = 0; i < win->menu_count; i++) {
        u32 tw = text_width(win->menu_items[i]) + 12;
        if (tw > item_w) item_w = tw;
    }
    if (item_w < 54) item_w = 54;
    u32 ix = bx;
    u32 iy = win->y + TITLE_H + 2;
    draw_rect(ix, iy, item_w, (u32)win->menu_count * MENU_H, win_style.menu);
    for (int i = 0; i < win->menu_count; i++) {
        draw_text(ix + 6, iy + (u32)i * MENU_H + 2, win->menu_items[i], 0x0F);
    }
}

static void draw_popup(gui_window_t *win) {
    if (!win->popup_open) return;
    u32 cx, cy, cw, ch;
    gui_window_get_content_rect(win, &cx, &cy, &cw, &ch);
    u32 pw = cw > 380 ? 380 : (cw > 40 ? cw - 20 : cw);
    u32 ph = 48;
    u32 px = cx + (cw > pw ? (cw - pw) / 2 : 0);
    u32 py = cy + (ch > ph ? (ch - ph) / 2 : 0);
    draw_rect(px, py, pw, ph, win_style.popup);
    draw_border(px, py, pw, ph, win_style.border);
    draw_text(px + 8, py + 6, win->popup_title, 0x0F);
    draw_text(px + 8, py + 24, win->popup_buf, 0x0F);
}

static void clamp_to_screen(gui_window_t *win) {
    u32 sw = 0, sh = 0;
    fb_get_resolution(&sw, &sh);
    if (!sw || !sh) return;
    if (win->w > sw) win->w = sw;
    u32 usable_h = sh > GUI_BAR_HEIGHT ? sh - GUI_BAR_HEIGHT : sh;
    if (win->h > usable_h) win->h = usable_h;
    if (win->x + win->w > sw) win->x = sw - win->w;
    if (win->y < GUI_BAR_HEIGHT + 2) win->y = GUI_BAR_HEIGHT + 2;
    if (win->y + win->h > sh) win->y = sh - win->h;
}

static void notify_move(gui_window_t *win) {
    u32 x = 0, y = 0, w = 0, h = 0;
    gui_window_get_content_rect(win, &x, &y, &w, &h);
    if (win->cb.move) win->cb.move(win, x, y, win->cb.ctx);
}

static void notify_resize(gui_window_t *win) {
    u32 x = 0, y = 0, w = 0, h = 0;
    gui_window_get_content_rect(win, &x, &y, &w, &h);
    if (win->cb.resize) win->cb.resize(win, w, h, win->cb.ctx);
    if (win->cb.move) win->cb.move(win, x, y, win->cb.ctx);
}

void gui_window_manager_init(void) {
    for (int i = 0; i < GUI_WINDOW_MAX; i++) {
        windows[i].used = 0;
        z_order[i] = -1;
    }
    z_count = 0;
    next_id = 1;
    active_id = -1;
    drag_mode = DRAG_NONE;
    drag_win = 0;
}

void gui_window_set_style(const gui_window_style_t *style) {
    if (!style) return;
    win_style = *style;
}

gui_window_t* gui_window_create(const char *title, u32 x, u32 y, u32 w, u32 h, u32 flags, gui_window_callbacks_t cb) {
    int slot = -1;
    for (int i = 0; i < GUI_WINDOW_MAX; i++) {
        if (!windows[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0;

    gui_window_t *win = &windows[slot];
    memset(win, 0, sizeof(*win));
    win->used = 1;
    win->id = gui_bar_register_window(title ? title : "Window");
    if (win->id <= 0) win->id = next_id++;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->min_w = 120;
    win->min_h = 80;
    win->flags = flags;
    win->cb = cb;
    copy_text(win->title, GUI_WINDOW_TITLE_MAX, title ? title : "Window");
    if (win->w < win->min_w) win->w = win->min_w;
    if (win->h < win->min_h) win->h = win->min_h;
    clamp_to_screen(win);
    z_order[z_count++] = slot;
    gui_window_focus(win);
    notify_resize(win);
    return win;
}

void gui_window_destroy(gui_window_t *win) {
    int slot = slot_of(win);
    if (slot < 0 || !win->used) return;
    if (win->cb.close) win->cb.close(win, win->cb.ctx);
    gui_bar_unregister_window(win->id);
    remove_from_z_order(slot);
    if (active_id == win->id) active_id = -1;
    win->used = 0;
    if (z_count > 0) {
        gui_window_t *top = &windows[z_order[z_count - 1]];
        if (top->used) gui_window_focus(top);
    }
}

void gui_window_destroy_all(void) {
    for (int i = GUI_WINDOW_MAX - 1; i >= 0; i--) {
        if (windows[i].used) gui_window_destroy(&windows[i]);
    }
    gui_window_manager_init();
}

void gui_window_set_title(gui_window_t *win, const char *title) {
    if (!win || !win->used) return;
    copy_text(win->title, GUI_WINDOW_TITLE_MAX, title ? title : "Window");
    gui_bar_update_window_title(win->id, win->title);
}

void gui_window_reserve_title_left(gui_window_t *win, const char *label) {
    if (!win || !win->used) return;
    win->title_left_inset = label ? text_width(label) + 18 : 0;
}

void gui_window_set_min_size(gui_window_t *win, u32 min_w, u32 min_h) {
    if (!win || !win->used) return;
    win->min_w = min_w;
    win->min_h = min_h;
    gui_window_resize(win, win->w, win->h);
}

void gui_window_add_menu_item(gui_window_t *win, const char *label) {
    if (!win || !win->used || !label) return;
    if (win->menu_count >= GUI_WINDOW_MENU_MAX) return;
    copy_text(win->menu_items[win->menu_count], GUI_WINDOW_MENU_TEXT_MAX, label);
    win->menu_count++;
}

void gui_window_open_popup(gui_window_t *win, const char *title, const char *initial) {
    if (!win || !win->used) return;
    copy_text(win->popup_title, GUI_WINDOW_MENU_TEXT_MAX, title ? title : "Input");
    copy_text(win->popup_buf, GUI_WINDOW_POPUP_TEXT_MAX, initial ? initial : "");
    win->popup_len = (u32)strlen(win->popup_buf);
    win->popup_open = 1;
    gui_window_focus(win);
}

int gui_window_popup_key(gui_window_t *win, u8 scancode, int is_extended, int shift) {
    if (!win || !win->used || !win->popup_open || is_extended) return 0;
    if (scancode == US_ESC) {
        win->popup_open = 0;
        return 1;
    }
    if (scancode == US_ENTER) {
        win->popup_open = 0;
        if (win->cb.popup_submit) win->cb.popup_submit(win, win->popup_buf, win->cb.ctx);
        return 1;
    }
    if (scancode == US_BACKSPACE) {
        if (win->popup_len > 0) {
            win->popup_len--;
            win->popup_buf[win->popup_len] = '\0';
        }
        return 1;
    }
    char c = scancode_to_char(scancode, shift);
    if (c && win->popup_len + 1 < GUI_WINDOW_POPUP_TEXT_MAX) {
        win->popup_buf[win->popup_len++] = c;
        win->popup_buf[win->popup_len] = '\0';
        return 1;
    }
    return 0;
}

gui_window_t* gui_window_active(void) {
    return gui_window_by_id(active_id);
}

void gui_window_focus(gui_window_t *win) {
    int slot = slot_of(win);
    if (slot < 0 || !win->used) return;
    remove_from_z_order(slot);
    z_order[z_count++] = slot;
    active_id = win->id;
    win->minimized = 0;
    close_all_menus_except(win);
    gui_bar_set_active_window(win->id);
}

gui_window_t* gui_window_by_id(int id) {
    for (int i = 0; i < GUI_WINDOW_MAX; i++) {
        if (windows[i].used && windows[i].id == id) return &windows[i];
    }
    return 0;
}

gui_window_t* gui_window_at(u32 x, u32 y) {
    for (int i = z_count - 1; i >= 0; i--) {
        gui_window_t *win = &windows[z_order[i]];
        if (!win->used || win->minimized) continue;
        if (point_in(x, y, win->x, win->y, win->w, win->h)) return win;
    }
    return 0;
}

void gui_window_get_content_rect(gui_window_t *win, u32 *x, u32 *y, u32 *w, u32 *h) {
    if (!win) return;
    u32 cx = win->x + CONTENT_PAD_X;
    u32 cy = win->y + BORDER_W + TITLE_H + 4;
    u32 cw = win->w > CONTENT_PAD_X * 2 ? win->w - CONTENT_PAD_X * 2 : 0;
    u32 ch = win->h > TITLE_H + CONTENT_PAD_BOTTOM + 4 ? win->h - TITLE_H - CONTENT_PAD_BOTTOM - 4 : 0;
    if (x) *x = cx;
    if (y) *y = cy;
    if (w) *w = cw;
    if (h) *h = ch;
}

void gui_window_move(gui_window_t *win, u32 x, u32 y) {
    if (!win || !win->used) return;
    win->x = x;
    win->y = y;
    clamp_to_screen(win);
    notify_move(win);
}

void gui_window_resize(gui_window_t *win, u32 w, u32 h) {
    if (!win || !win->used) return;
    if (win->flags & GUI_WINDOW_FIXED_SIZE) return;
    if (w < win->min_w) w = win->min_w;
    if (h < win->min_h) h = win->min_h;
    win->w = w;
    win->h = h;
    clamp_to_screen(win);
    notify_resize(win);
}

static void gui_window_preview_move(gui_window_t *win, u32 x, u32 y) {
    if (!win || !win->used) return;
    drag_preview_x = x;
    drag_preview_y = y;
    u32 sw = 0, sh = 0;
    fb_get_resolution(&sw, &sh);
    if (!sw || !sh) return;
    if (drag_preview_w > sw) drag_preview_w = sw;
    if (drag_preview_h > sh) drag_preview_h = sh;
    if (drag_preview_x + drag_preview_w > sw) drag_preview_x = sw - drag_preview_w;
    if (drag_preview_y < GUI_BAR_HEIGHT + 2) drag_preview_y = GUI_BAR_HEIGHT + 2;
    if (drag_preview_y + drag_preview_h > sh) drag_preview_y = sh - drag_preview_h;
}

static void gui_window_preview_resize(gui_window_t *win, u32 w, u32 h) {
    if (!win || !win->used || (win->flags & GUI_WINDOW_FIXED_SIZE)) return;
    if (w < win->min_w) w = win->min_w;
    if (h < win->min_h) h = win->min_h;
    u32 sw = 0, sh = 0;
    fb_get_resolution(&sw, &sh);
    if (sw && w > sw) w = sw;
    u32 usable_h = sh > GUI_BAR_HEIGHT ? sh - GUI_BAR_HEIGHT : sh;
    if (usable_h && h > usable_h) h = usable_h;
    if (sw && win->x + w > sw) w = sw - win->x;
    if (sh && win->y + h > sh) h = sh - win->y;
    if (w < win->min_w) w = win->min_w;
    if (h < win->min_h) h = win->min_h;
    drag_preview_w = w;
    drag_preview_h = h;
}

void gui_window_minimize(gui_window_t *win) {
    if (!win || !win->used || (win->flags & GUI_WINDOW_NO_MINIMIZE)) return;
    win->minimized = 1;
    if (win->cb.minimize) win->cb.minimize(win, win->cb.ctx);
    if (active_id == win->id) active_id = -1;
    for (int i = z_count - 1; i >= 0; i--) {
        gui_window_t *candidate = &windows[z_order[i]];
        if (candidate->used && !candidate->minimized) {
            gui_window_focus(candidate);
            break;
        }
    }
}

void gui_window_restore(gui_window_t *win) {
    if (!win || !win->used) return;
    win->minimized = 0;
    gui_window_focus(win);
}

void gui_window_render_frame(gui_window_t *win) {
    if (!win || !win->used || win->minimized) return;
    draw_rect(win->x, win->y, win->w, win->h, win_style.window);
    draw_border(win->x, win->y, win->w, win->h, win_style.border);
    draw_rect(win->x + BORDER_W, win->y + BORDER_W, win->w - BORDER_W * 2, TITLE_H, win->id == active_id ? win_style.active_title : win_style.title);
    draw_menu(win);
    draw_text(title_text_x(win), win->y + 5, win->title, 0x0F);

    u32 bx, by, bw, bh;
    if (!(win->flags & GUI_WINDOW_NO_MINIMIZE)) {
        minimize_button_rect(win, &bx, &by, &bw, &bh);
        draw_rect(bx, by, bw, bh, win_style.minimize_button);
    }
    if (!(win->flags & GUI_WINDOW_NO_CLOSE)) {
        close_button_rect(win, &bx, &by, &bw, &bh);
        draw_rect(bx, by, bw, bh, win_style.close_button);
    }
    if (!(win->flags & GUI_WINDOW_FIXED_SIZE)) {
        u8 grip[3] = { 0x88, 0x88, 0x88 };
        draw_rect(win->x + win->w - RESIZE_GRIP, win->y + win->h - 3, RESIZE_GRIP - 2, 1, grip);
        draw_rect(win->x + win->w - 3, win->y + win->h - RESIZE_GRIP, 1, RESIZE_GRIP - 2, grip);
    }
}

void gui_window_render(gui_window_t *win) {
    if (!win || !win->used || win->minimized) return;
    gui_window_render_frame(win);
    if (win->cb.render) win->cb.render(win, win->cb.ctx);
    draw_popup(win);
}

void gui_window_render_all(void) {
    for (int i = 0; i < z_count; i++) {
        gui_window_render(&windows[z_order[i]]);
    }
}

void gui_window_render_drag_preview_all(void) {
    for (int i = 0; i < z_count; i++) {
        gui_window_t *win = &windows[z_order[i]];
        if (!win->used || win->minimized) continue;
        if (win == drag_win && drag_mode != DRAG_NONE) {
            draw_outline(drag_preview_x, drag_preview_y, drag_preview_w, drag_preview_h);
        } else {
            draw_outline(win->x, win->y, win->w, win->h);
        }
    }
}

int gui_window_is_dragging(void) {
    return drag_mode != DRAG_NONE;
}

int gui_window_mouse_down(u32 x, u32 y) {
    gui_window_t *win = gui_window_at(x, y);
    if (!win) {
        close_all_menus_except(0);
        return 0;
    }
    gui_window_focus(win);

    if (win->popup_open) return 1;

    if (win->menu_count > 0) {
        u32 bx = win->x + 8;
        u32 by = win->y + 3;
        u32 bw = text_width("File") + 12;
        u32 bh = TITLE_H - 6;
        if (point_in(x, y, bx, by, bw, bh)) {
            win->menu_open = !win->menu_open;
            return 1;
        }
        if (win->menu_open) {
            u32 item_w = 54;
            for (int i = 0; i < win->menu_count; i++) {
                u32 tw = text_width(win->menu_items[i]) + 12;
                if (tw > item_w) item_w = tw;
            }
            u32 ix = bx;
            u32 iy = win->y + TITLE_H + 2;
            for (int i = 0; i < win->menu_count; i++) {
                if (point_in(x, y, ix, iy + (u32)i * MENU_H, item_w, MENU_H)) {
                    win->menu_open = 0;
                    if (win->cb.menu) win->cb.menu(win, i, win->cb.ctx);
                    return 1;
                }
            }
            win->menu_open = 0;
            return 1;
        }
    }

    u32 bx, by, bw, bh;
    if (!(win->flags & GUI_WINDOW_NO_CLOSE)) {
        close_button_rect(win, &bx, &by, &bw, &bh);
        if (point_in(x, y, bx, by, bw, bh)) {
            gui_window_destroy(win);
            return 1;
        }
    }
    if (!(win->flags & GUI_WINDOW_NO_MINIMIZE)) {
        minimize_button_rect(win, &bx, &by, &bw, &bh);
        if (point_in(x, y, bx, by, bw, bh)) {
            gui_window_minimize(win);
            return 1;
        }
    }
    if (!(win->flags & GUI_WINDOW_FIXED_SIZE) &&
        point_in(x, y, win->x + win->w - RESIZE_GRIP, win->y + win->h - RESIZE_GRIP, RESIZE_GRIP, RESIZE_GRIP)) {
        drag_mode = DRAG_RESIZE;
        drag_win = win;
        drag_start_x = x;
        drag_start_y = y;
        drag_start_w = win->w;
        drag_start_h = win->h;
        drag_last_x = x;
        drag_last_y = y;
        drag_preview_x = win->x;
        drag_preview_y = win->y;
        drag_preview_w = win->w;
        drag_preview_h = win->h;
        return 1;
    }
    if (point_in(x, y, win->x, win->y, win->w, TITLE_H + BORDER_W * 2)) {
        drag_mode = DRAG_MOVE;
        drag_win = win;
        drag_off_x = x - win->x;
        drag_off_y = y - win->y;
        drag_last_x = x;
        drag_last_y = y;
        drag_preview_x = win->x;
        drag_preview_y = win->y;
        drag_preview_w = win->w;
        drag_preview_h = win->h;
        return 1;
    }
    return 1;
}

static void apply_drag_at(u32 x, u32 y, int notify) {
    if (!drag_win || drag_mode == DRAG_NONE) return;
    if (drag_mode == DRAG_MOVE) {
        u32 nx = x > drag_off_x ? x - drag_off_x : 0;
        u32 ny = y > drag_off_y ? y - drag_off_y : 0;
        if (notify) gui_window_move(drag_win, nx, ny);
        else gui_window_preview_move(drag_win, nx, ny);
    } else if (drag_mode == DRAG_RESIZE) {
        int dx = (int)x - (int)drag_start_x;
        int dy = (int)y - (int)drag_start_y;
        u32 nw = dx < 0 && (u32)(-dx) > drag_start_w ? drag_win->min_w : drag_start_w + dx;
        u32 nh = dy < 0 && (u32)(-dy) > drag_start_h ? drag_win->min_h : drag_start_h + dy;
        if (notify) gui_window_resize(drag_win, nw, nh);
        else gui_window_preview_resize(drag_win, nw, nh);
    }
}

int gui_window_mouse_drag(u32 x, u32 y) {
    if (!drag_win || drag_mode == DRAG_NONE) return 0;
    if (abs_i((int)x - (int)drag_last_x) < DRAG_STEP_PIXELS &&
        abs_i((int)y - (int)drag_last_y) < DRAG_STEP_PIXELS) {
        return 0;
    }
    apply_drag_at(x, y, 0);
    drag_last_x = x;
    drag_last_y = y;
    return 1;
}

int gui_window_mouse_up(u32 x, u32 y) {
    if (drag_mode == DRAG_NONE) return 0;
    apply_drag_at(x, y, 1);
    drag_mode = DRAG_NONE;
    drag_win = 0;
    return 1;
}
