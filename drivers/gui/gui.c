#include <cldtypes.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <vgaio.h>
#include <shell_control.h>
#include <kmalloc.h>
#include "term.h"

// Simple GUI state
static int gui_active = 0;
static u32 scr_w = 0, scr_h = 0;
static u32 win_x = 0, win_y = 0, win_w = 0, win_h = 0;
static u32 cursor_x = 0, cursor_y = 0;
static const u32 CURSOR_SZ = 8;
static int dragging = 0;
static u32 drag_off_x = 0, drag_off_y = 0;
static u8 last_buttons = 0;

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

static void gui_clear_all(void) {
    draw_rect_rgb(0, 0, scr_w, scr_h, COL_BG);
}

static void gui_draw_window(void) {
    // Window body
    draw_rect_rgb(win_x, win_y, win_w, win_h, COL_WIN);
    draw_border(win_x, win_y, win_w, win_h, 2, COL_BORDER);

    // Title bar
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    draw_rect_rgb(win_x + 2, win_y + 2, win_w - 4, title_h, COL_TITLE);
}

// Cursor background save/restore to reduce flicker
static u8* cursor_save = 0;
static u32 cursor_save_x = 0, cursor_save_y = 0;
static int cursor_saved = 0;
static u8 fb_bpp = 0;

static void gui_cursor_undraw(void) {
    if (cursor_saved && cursor_save) {
        fb_blit(cursor_save_x, cursor_save_y, CURSOR_SZ, CURSOR_SZ, cursor_save);
        cursor_saved = 0;
    }
}

static void gui_cursor_draw(u32 x, u32 y) {
    if (!cursor_save) return;
    fb_copy_out(x, y, CURSOR_SZ, CURSOR_SZ, cursor_save);
    cursor_save_x = x; cursor_save_y = y;
    cursor_saved = 1;
    draw_rect_rgb(x, y, CURSOR_SZ, CURSOR_SZ, COL_CURSOR);
}

static void gui_mouse_cb(int dx, int dy, u8 buttons) {
    if (!gui_active) return;
    u32 old_cx = cursor_x, old_cy = cursor_y;
    u32 old_wx = win_x, old_wy = win_y;

    // Update cursor position with clamping
    int nx = (int)cursor_x + dx;
    int ny = (int)cursor_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > (int)(scr_w - CURSOR_SZ)) nx = (int)(scr_w - CURSOR_SZ);
    if (ny > (int)(scr_h - CURSOR_SZ)) ny = (int)(scr_h - CURSOR_SZ);
    cursor_x = (u32)nx;
    cursor_y = (u32)ny;

    int left_pressed = (buttons & 0x01) != 0;
    int left_was_pressed = (last_buttons & 0x01) != 0;

    // Start drag if left just pressed inside title bar
    if (!dragging && left_pressed && !left_was_pressed) {
        u32 tb_h = (win_h > 24) ? 24 : (win_h / 8);
        u32 tx1 = win_x + 2;
        u32 ty1 = win_y + 2;
        u32 tx2 = win_x + win_w - 2;
        u32 ty2 = ty1 + tb_h;
        if (cursor_x >= tx1 && cursor_x < tx2 && cursor_y >= ty1 && cursor_y < ty2) {
            dragging = 1;
            drag_off_x = cursor_x - win_x;
            drag_off_y = cursor_y - win_y;
        }
    }

    // Stop drag on release
    if (dragging && !left_pressed && left_was_pressed) {
        dragging = 0;
    }

    // If dragging, move window and redraw
    int window_moved = 0;
    if (dragging) {
        int new_wx = (int)cursor_x - (int)drag_off_x;
        int new_wy = (int)cursor_y - (int)drag_off_y;
        if (new_wx < 0) new_wx = 0;
        if (new_wy < 0) new_wy = 0;
        if (new_wx > (int)(scr_w - win_w)) new_wx = (int)(scr_w - win_w);
        if (new_wy > (int)(scr_h - win_h)) new_wy = (int)(scr_h - win_h);
        if (win_x != (u32)new_wx || win_y != (u32)new_wy) {
            win_x = (u32)new_wx;
            win_y = (u32)new_wy;
            window_moved = 1;
        }
    }

    if (window_moved) {
        // Remove old cursor drawing so it doesn't interfere
        gui_cursor_undraw();
        // Clear old window area fully
        draw_rect_rgb(old_wx, old_wy, win_w, win_h, COL_BG);
        // Draw new window frame
        gui_draw_window();
        // Redraw terminal content from backing store at new position
        u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
        u32 ncx = win_x + 6;
        u32 ncy = win_y + 2 + title_h + 4;
        gui_term_move(ncx, ncy);
        gui_term_render_all();
        // Draw cursor at new position
        gui_cursor_draw(cursor_x, cursor_y);
    } else {
        // Just move the cursor with save/restore
        gui_cursor_undraw();
        gui_cursor_draw(cursor_x, cursor_y);
    }

    last_buttons = buttons;
}

static void gui_stop(void);

static void gui_key_handler(u8 scancode, int is_extended, int is_pressed) {
    (void)is_extended;
    if (!gui_active) return;
    if (is_pressed && scancode == US_ESC) {
        gui_stop();
    }
    // Forward all keys to TTY/shell for processing
    if (is_pressed) {
        extern int tty_global_handle_key(u8 scancode, int is_extended);
        extern void cldramfs_shell_handle_input(void);
        if (tty_global_handle_key(scancode, is_extended)) {
            cldramfs_shell_handle_input();
        }
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

    // Prepare cursor save buffer
    fb_bpp = fb_get_bytespp();
    if (fb_bpp == 0) return;
    cursor_save = (u8*)kmalloc(CURSOR_SZ * CURSOR_SZ * fb_bpp);
    if (!cursor_save) return;

    // Draw initial GUI
    gui_clear_all();
    gui_draw_window();
    // Terminal area: inside window, below title bar with margins
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    u32 content_x = win_x + 6;
    u32 content_y = win_y + 2 + title_h + 4;
    u32 content_w = (win_w > 12) ? (win_w - 12) : 0;
    u32 content_h = (win_h > title_h + 10) ? (win_h - title_h - 10) : 0;
    gui_term_init(content_x, content_y, content_w, content_h);
    gui_term_attach();
    gui_cursor_draw(cursor_x, cursor_y);

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
    // Detach terminal sink
    gui_term_detach();

    // Clear screen back to text console background
    vga_clear_screen();

    // Restore shell input and prompt
    shell_resume();

    if (cursor_save) {
        kfree(cursor_save);
        cursor_save = 0;
        cursor_saved = 0;
    }

    // Free terminal backing store
    gui_term_free();
}
