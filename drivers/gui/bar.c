#include "bar.h"
#include <fb/fb_console.h>
#include <kmalloc.h>
#include <string.h>

#define MAX_MENUS   8
#define MAX_WINDOWS 8

typedef struct { char* label; } menu_item_t;
typedef struct { int id; char* title; int active; u32 rx; u32 rw; } win_item_t;

static int bar_inited = 0;
static menu_item_t menus[MAX_MENUS];
static int menu_count = 0;
static win_item_t windows[MAX_WINDOWS];
static int window_count = 0;
static int next_win_id = 1;
static int menu_open = 0;
static u32 menu_btn_x = 0, menu_btn_w = 0; // Menu button rect (y is fixed)
static u32 menu_item_x = 0, menu_item_y = 0, menu_item_w = 0, menu_item_h = 0; // New Terminal item
static u32 menu_item2_x = 0, menu_item2_y = 0, menu_item2_w = 0, menu_item2_h = 0; // New Editor item
static u32 last_rect_x = 0, last_rect_y = 0, last_rect_w = 0, last_rect_h = 0;
static int last_rect_valid = 0;
// Live dropdown rect while open (covers all items)
static u32 menu_rect_x = 0, menu_rect_y = 0, menu_rect_w = 0, menu_rect_h = 0;

// Colors
static const u8 BAR_BG[3]    = { 0x25, 0x25, 0x28 };
static const u8 BAR_ACC[3]   = { 0x66, 0x66, 0x6A }; // stylish gray accent
static const u8 SEP_COL[3]   = { 0x40, 0x40, 0x44 };
static const u8 TXT_COL[3]   = { 0xEE, 0xEE, 0xEE };
static const u8 TXT_DIM[3]   = { 0xAA, 0xAA, 0xAA };

static void draw_rect_rgb(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    fb_fill_rect_rgb(x, y, w, h, rgb[0], rgb[1], rgb[2]);
}

static void draw_text(u32 x, u32 y, const char* s, u8 vga_attr) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u32 px = x, py = y;
    for (; *s; s++) {
        // Draw without background so bar bg shows through
        u8 fg = vga_attr & 0x0F;
        fb_draw_char_px_nobg(px, py, *s, fg);
        px += (u32)cw;
    }
}

static u32 text_width_px(const char* s) {
    int cw = 8, ch = 16;
    (void)fb_font_get_cell_size(&cw, &ch);
    u32 n = 0; while (s && s[n]) n++;
    return (u32)cw * n;
}

void gui_bar_init(void) {
    if (bar_inited) return;
    bar_inited = 1;
    // Single menu button for now
    gui_bar_clear_menus();
    gui_bar_add_menu("Menu");
}

void gui_bar_render(void) {
    u32 w = 0, h = 0; fb_get_resolution(&w, &h);
    if (w == 0 || h == 0) return;
    draw_rect_rgb(0, 0, w, GUI_BAR_HEIGHT, BAR_BG);

    // Menus on left (only first used)
    u32 x = 8; u32 y = 4;
    if (menu_count > 0 && menus[0].label) {
        u32 tw = text_width_px(menus[0].label) + 12;
        // Menu button background (slight accent if open)
        if (menu_open) draw_rect_rgb(x, 2, tw, GUI_BAR_HEIGHT - 4, SEP_COL);
        draw_text(x + 6, y, menus[0].label, 0x0F);
        menu_btn_x = x; menu_btn_w = tw;
        x += tw + 8;
        draw_rect_rgb(x - 4, 4, 1, GUI_BAR_HEIGHT - 8, SEP_COL);
    } else {
        menu_btn_x = 0; menu_btn_w = 0;
    }

    // Windows/task list area
    u32 pad = 8;
    x = pad; // reuse left side for simplicity
    // Compute start x after menus
    for (int i = 0; i < menu_count; i++) {
        if (!menus[i].label) continue;
        x += text_width_px(menus[i].label) + 16;
    }
    if (x < w) {
        draw_rect_rgb(x, GUI_BAR_HEIGHT - 2, w - x, 2, SEP_COL);
    }

    u32 wx = x + 12;
    for (int i = 0; i < window_count; i++) {
        if (!windows[i].title) continue;
        u32 tw = text_width_px(windows[i].title) + 12;
        if (wx + tw + pad > w) break;
        // Active highlight
        if (windows[i].active) {
            draw_rect_rgb(wx - 6, 2, tw, GUI_BAR_HEIGHT - 4, BAR_ACC);
            draw_text(wx - 0, 4, windows[i].title, 0x0F);
        } else {
            draw_text(wx, 4, windows[i].title, 0x07);
        }
        windows[i].rx = wx - 6; windows[i].rw = tw;
        wx += tw + 8;
    }

    // Dropdown menu if open
    if (menu_open && menu_count > 0 && menus[0].label) {
        const char* item1 = "New Terminal";
        const char* item2 = "New Editor";
        u32 itw1 = text_width_px(item1) + 12;
        u32 itw2 = text_width_px(item2) + 12;
        u32 itw  = (itw1 > itw2) ? itw1 : itw2;
        u32 ix = menu_btn_x;
        u32 iy = GUI_BAR_HEIGHT; // flush under bar to avoid hover gap
        u32 ih = 18;
        // Item 1
        draw_rect_rgb(ix, iy, itw, ih, SEP_COL);
        draw_text(ix + 6, iy + 2, item1, 0x0F);
        // Item 2
        draw_rect_rgb(ix, iy + ih, itw, ih, SEP_COL);
        draw_text(ix + 6, iy + ih + 2, item2, 0x0F);
        // Store hitboxes
        menu_item_x = ix; menu_item_y = iy; menu_item_w = itw; menu_item_h = ih;
        menu_item2_x = ix; menu_item2_y = iy + ih; menu_item2_w = itw; menu_item2_h = ih;
        // Store whole dropdown rect (two items)
        menu_rect_x = ix; menu_rect_y = iy; menu_rect_w = itw; menu_rect_h = ih * 2;
    } else {
        menu_item_x = menu_item_y = menu_item_w = menu_item_h = 0;
        menu_item2_x = menu_item2_y = menu_item2_w = menu_item2_h = 0;
        menu_rect_x = menu_rect_y = menu_rect_w = menu_rect_h = 0;
    }
}

int gui_bar_add_menu(const char* label) {
    if (menu_count >= MAX_MENUS || !label) return -1;
    size_t len = strlen(label);
    char* s = (char*)kmalloc(len + 1);
    if (!s) return -1;
    strcpy(s, label);
    menus[menu_count].label = s;
    return menu_count++;
}

void gui_bar_clear_menus(void) {
    for (int i = 0; i < menu_count; i++) {
        if (menus[i].label) { kfree(menus[i].label); menus[i].label = 0; }
    }
    menu_count = 0;
}

int gui_bar_register_window(const char* title) {
    if (window_count >= MAX_WINDOWS || !title) return -1;
    int slot = window_count++;
    size_t len = strlen(title);
    char* s = (char*)kmalloc(len + 1);
    if (!s) { window_count--; return -1; }
    strcpy(s, title);
    windows[slot].title = s;
    windows[slot].id = next_win_id++;
    windows[slot].active = 0;
    return windows[slot].id;
}

void gui_bar_unregister_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            if (windows[i].title) { kfree(windows[i].title); windows[i].title = 0; }
            // Compact array
            for (int j = i; j < window_count - 1; j++) windows[j] = windows[j+1];
            window_count--;
            break;
        }
    }
}

void gui_bar_set_active_window(int id) {
    for (int i = 0; i < window_count; i++) windows[i].active = (windows[i].id == id);
}

int gui_bar_on_click(u32 x, u32 y, int* out_window_id) {
    // Click on menu button toggles dropdown
    if (y < GUI_BAR_HEIGHT) {
        if (menu_btn_w && x >= menu_btn_x && x < menu_btn_x + menu_btn_w) {
        menu_open = !menu_open;
        gui_bar_render();
        return 0;
    }
    }
    // Dropdown handling when open
    if (menu_open) {
        // New Terminal
        if (x >= menu_item_x && x < menu_item_x + menu_item_w &&
            y >= menu_item_y && y < menu_item_y + menu_item_h) {
            last_rect_x = menu_rect_x; last_rect_y = menu_rect_y;
            last_rect_w = menu_rect_w; last_rect_h = menu_rect_h;
            last_rect_valid = 1;
            menu_open = 0;
            gui_bar_render();
            return 1;
        }
        // New Editor
        if (x >= menu_item2_x && x < menu_item2_x + menu_item2_w &&
            y >= menu_item2_y && y < menu_item2_y + menu_item2_h) {
            last_rect_x = menu_rect_x; last_rect_y = menu_rect_y;
            last_rect_w = menu_rect_w; last_rect_h = menu_rect_h;
            last_rect_valid = 1;
            menu_open = 0;
            gui_bar_render();
            return 3;
        }
        // Clicked elsewhere while open: close menu
        last_rect_x = menu_rect_x; last_rect_y = menu_rect_y;
        last_rect_w = menu_rect_w; last_rect_h = menu_rect_h;
        last_rect_valid = 1;
        menu_open = 0;
        gui_bar_render();
    }

    // Click on window tabs
    if (y < GUI_BAR_HEIGHT) {
        for (int i = 0; i < window_count; i++) {
            if (windows[i].rw == 0) continue;
            u32 rx = windows[i].rx, rw = windows[i].rw;
            if (x >= rx && x < rx + rw) {
                gui_bar_set_active_window(windows[i].id);
                gui_bar_render();
                if (out_window_id) *out_window_id = windows[i].id;
                return 2; // focus window
            }
        }
    }
    return 0;
}

int gui_bar_on_move(u32 x, u32 y) {
    // If menu is open and cursor is outside both the menu button and dropdown item, close it
    if (!menu_open) return 0;
    int over_btn = (menu_btn_w && x >= menu_btn_x && x < menu_btn_x + menu_btn_w && y < GUI_BAR_HEIGHT);
    int over_drop = (menu_rect_w && x >= menu_rect_x && x < menu_rect_x + menu_rect_w &&
                     y >= menu_rect_y && y < menu_rect_y + menu_rect_h);
    if (!over_btn && !over_drop) {
        // Save rect and close
        last_rect_x = menu_rect_x; last_rect_y = menu_rect_y;
        last_rect_w = menu_rect_w; last_rect_h = menu_rect_h;
        last_rect_valid = 1;
        menu_open = 0;
        gui_bar_render();
        return 1;
    }
    return 0;
}

int gui_bar_get_last_dropdown_rect(u32* x, u32* y, u32* w, u32* h) {
    if (!last_rect_valid) return 0;
    if (x) *x = last_rect_x;
    if (y) *y = last_rect_y;
    if (w) *w = last_rect_w;
    if (h) *h = last_rect_h;
    last_rect_valid = 0;
    return 1;
}

int gui_bar_is_menu_open(void) { return menu_open; }

int gui_bar_get_current_dropdown_rect(u32* x, u32* y, u32* w, u32* h) {
    if (!menu_open || menu_rect_w == 0 || menu_rect_h == 0) return 0;
    if (x) *x = menu_rect_x; if (y) *y = menu_rect_y; if (w) *w = menu_rect_w; if (h) *h = menu_rect_h;
    return 1;
}
