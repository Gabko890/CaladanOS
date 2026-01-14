#include <cldtypes.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <vgaio.h>
#include <shell_control.h>

// Simple GUI state
static int gui_active = 0;
static u32 scr_w = 0, scr_h = 0;
static u32 win_x = 0, win_y = 0, win_w = 0, win_h = 0;
static u32 cursor_x = 0, cursor_y = 0;
static const u32 CURSOR_SZ = 8;

// Colors
static const u8 COL_BG[3]     = { 0x20, 0x20, 0x20 };
static const u8 COL_WIN[3]    = { 0xCC, 0xCC, 0xCC };
static const u8 COL_TITLE[3]  = { 0x33, 0x66, 0x99 };
static const u8 COL_BORDER[3] = { 0x00, 0x00, 0x00 };
static const u8 COL_CURSOR[3] = { 0x00, 0x00, 0x00 };

static void draw_rect_rgb(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    fb_fill_rect_rgb(x, y, w, h, rgb[0], rgb[1], rgb[2]);
}

static void draw_border(u32 x, u32 y, u32 w, u32 h, u32 t, const u8 rgb[3]) {
    if (w == 0 || h == 0) return;
    // Top
    draw_rect_rgb(x, y, w, t, rgb);
    // Bottom
    if (h > t) draw_rect_rgb(x, y + h - t, w, t, rgb);
    // Left
    if (h > 2 * t) draw_rect_rgb(x, y + t, t, h - 2 * t, rgb);
    // Right
    if (w > t && h > 2 * t) draw_rect_rgb(x + w - t, y + t, t, h - 2 * t, rgb);
}

static void gui_draw_window(void) {
    // Background
    draw_rect_rgb(0, 0, scr_w, scr_h, COL_BG);

    // Window body
    draw_rect_rgb(win_x, win_y, win_w, win_h, COL_WIN);
    draw_border(win_x, win_y, win_w, win_h, 2, COL_BORDER);

    // Title bar
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    draw_rect_rgb(win_x + 2, win_y + 2, win_w - 4, title_h, COL_TITLE);
}

static void gui_draw_cursor(u32 x, u32 y) {
    // Draw simple filled square cursor
    draw_rect_rgb(x, y, CURSOR_SZ, CURSOR_SZ, COL_CURSOR);
}

static void gui_erase_cursor(u32 x, u32 y) {
    // Erase by redrawing the area underneath (window or background)
    u32 ex = x, ey = y, ew = CURSOR_SZ, eh = CURSOR_SZ;

    // Compute intersection with window
    u32 wx1 = win_x, wy1 = win_y, wx2 = win_x + win_w, wy2 = win_y + win_h;
    u32 cx1 = ex, cy1 = ey, cx2 = ex + ew, cy2 = ey + eh;

    // Part overlapping the window
    u32 ox1 = (cx1 > wx1) ? cx1 : wx1;
    u32 oy1 = (cy1 > wy1) ? cy1 : wy1;
    u32 ox2 = (cx2 < wx2) ? cx2 : wx2;
    u32 oy2 = (cy2 < wy2) ? cy2 : wy2;

    // First, redraw background everywhere
    draw_rect_rgb(ex, ey, ew, eh, COL_BG);

    // Then, if overlapping window, redraw that portion on top
    if (ox2 > ox1 && oy2 > oy1) {
        draw_rect_rgb(ox1, oy1, ox2 - ox1, oy2 - oy1, COL_WIN);
        // Title bar overlap
        u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
        u32 tb_y1 = win_y + 2;
        u32 tb_y2 = tb_y1 + title_h;
        u32 tx1 = (ox1 > (win_x + 2)) ? ox1 : (win_x + 2);
        u32 ty1 = (oy1 > tb_y1) ? oy1 : tb_y1;
        u32 tx2 = (ox2 < (win_x + win_w - 2)) ? ox2 : (win_x + win_w - 2);
        u32 ty2 = (oy2 < tb_y2) ? oy2 : tb_y2;
        if (tx2 > tx1 && ty2 > ty1) {
            draw_rect_rgb(tx1, ty1, tx2 - tx1, ty2 - ty1, COL_TITLE);
        }
        // Redraw border if needed (approximate; keep it simple)
        draw_border(win_x, win_y, win_w, win_h, 2, COL_BORDER);
    }
}

static void gui_mouse_cb(int dx, int dy, u8 buttons) {
    if (!gui_active) return;
    (void)buttons;
    u32 old_x = cursor_x, old_y = cursor_y;

    // Update cursor position with clamping
    int nx = (int)cursor_x + dx;
    int ny = (int)cursor_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > (int)(scr_w - CURSOR_SZ)) nx = (int)(scr_w - CURSOR_SZ);
    if (ny > (int)(scr_h - CURSOR_SZ)) ny = (int)(scr_h - CURSOR_SZ);
    cursor_x = (u32)nx;
    cursor_y = (u32)ny;

    // Erase old cursor and draw new one
    gui_erase_cursor(old_x, old_y);
    gui_draw_cursor(cursor_x, cursor_y);
}

static void gui_stop(void);

static void gui_key_handler(u8 scancode, int is_extended, int is_pressed) {
    (void)is_extended;
    if (!gui_active) return;
    if (is_pressed && scancode == US_ESC) {
        gui_stop();
    }
}

void gui_start(void) {
    if (!fb_console_present()) {
        return; // No framebuffer; GUI not available
    }

    fb_get_resolution(&scr_w, &scr_h);
    if (scr_w == 0 || scr_h == 0) return;

    // Configure a basic window roughly centered
    win_w = scr_w / 2;
    win_h = scr_h / 2;
    if (win_w < 100) win_w = (scr_w > 120) ? 120 : scr_w - 20;
    if (win_h < 80)  win_h = (scr_h > 100) ? 100 : scr_h - 20;
    win_x = (scr_w - win_w) / 2;
    win_y = (scr_h - win_h) / 3;

    // Initial cursor position in center
    cursor_x = scr_w / 2;
    cursor_y = scr_h / 2;
    if (cursor_x > scr_w - CURSOR_SZ) cursor_x = scr_w - CURSOR_SZ;
    if (cursor_y > scr_h - CURSOR_SZ) cursor_y = scr_h - CURSOR_SZ;

    // Draw initial GUI
    gui_draw_window();
    gui_draw_cursor(cursor_x, cursor_y);

    // Hook input callbacks and mark active
    ps2_mouse_set_callback(gui_mouse_cb);
    ps2_set_key_callback(gui_key_handler);
    gui_active = 1;
}

static void gui_stop(void) {
    if (!gui_active) return;
    gui_active = 0;
    // Unhook mouse callback
    ps2_mouse_set_callback(0);

    // Clear screen back to text console background
    vga_clear_screen();

    // Restore shell input and prompt
    shell_resume();
}
