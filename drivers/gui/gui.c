#include <cldtypes.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <vgaio.h>
#include <shell_control.h>
#include <kmalloc.h>
#include "term.h"
#include "editor.h"
#include "cursor_bitmap.h"
#include "bar.h"
#include "wallpaper.h"

// Simple GUI state
static int gui_active = 0;
static int terminal_win_id = -1;
static int editor_win_id = -1;
static int window_open = 0;
typedef enum { WIN_NONE = 0, WIN_TERMINAL = 1, WIN_EDITOR = 2 } win_kind_t;
static win_kind_t cur_win = WIN_NONE;
static u32 scr_w = 0, scr_h = 0;
static u32 win_x = 0, win_y = 0, win_w = 0, win_h = 0;
static u32 cursor_x = 0, cursor_y = 0;
// Cursor dimensions come from bitmap
static int dragging = 0;
static u32 drag_off_x = 0, drag_off_y = 0;
static u8 last_buttons = 0;

// Colors
static const u8 COL_BG[3]     = { 0x20, 0x20, 0x20 };
static const u8 COL_WIN[3]    = { 0xCC, 0xCC, 0xCC };
static const u8 COL_TITLE[3]  = { 0x60, 0x60, 0x64 }; // stylish gray
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

static void restore_bg_rect(u32 x, u32 y, u32 w, u32 h) {
    if (gui_wallpaper_is_loaded()) gui_wallpaper_redraw_rect(x, y, w, h);
    else draw_rect_rgb(x, y, w, h, COL_BG);
}

static void gui_clear_all(void) {
    if (gui_wallpaper_is_loaded()) gui_wallpaper_draw_fullscreen();
    else draw_rect_rgb(0, 0, scr_w, scr_h, COL_BG);
}

static void gui_draw_window(void) {
    // Window body
    draw_rect_rgb(win_x, win_y, win_w, win_h, COL_WIN);
    draw_border(win_x, win_y, win_w, win_h, 2, COL_BORDER);

    // Title bar
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    draw_rect_rgb(win_x + 2, win_y + 2, win_w - 4, title_h, COL_TITLE);
    // Close button (red square) in title bar right
    const u8 RED[3] = { 0xCC, 0x33, 0x33 };
    u32 cb = (title_h > 14) ? 12 : (title_h - 4);
    if (cb > 6) {
        u32 cx = win_x + win_w - cb - 6;
        draw_rect_rgb(cx, win_y + 4, cb, cb, RED);
    }
}

// Cursor background save/restore to reduce flicker
static u8* cursor_save = 0;
static u32 cursor_save_x = 0, cursor_save_y = 0;
static u32 cursor_save_w = 0, cursor_save_h = 0; // actual saved dimensions (clamped at screen edges)
static int cursor_saved = 0;
static u8 fb_bpp = 0;

// Full re-render helper: background, window (if any), bar
static void gui_rerender_full(void) {
    gui_clear_all();
    if (window_open) {
        gui_draw_window();
        // Recompute terminal anchor and redraw content
        u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
        (void)title_h; // only for clarity; anchor is already set by init/move
        if (cur_win == WIN_TERMINAL) {
            gui_term_render_all();
        } else if (cur_win == WIN_EDITOR) {
            // Provide titlebar geometry to editor and draw its overlays on title bar
            gui_editor_set_titlebar(win_x, win_y, win_w, title_h);
            gui_editor_render_all();
            gui_editor_draw_overlays();
        }
    }
    gui_bar_render();
}


static void gui_cursor_undraw(void) {
    if (cursor_saved && cursor_save) {
        // Restore only the region actually saved (may be smaller near edges)
        if (cursor_save_w > 0 && cursor_save_h > 0) {
            fb_blit(cursor_save_x, cursor_save_y, cursor_save_w, cursor_save_h, cursor_save);
        }
        cursor_saved = 0;
    }
}

static void gui_cursor_draw(u32 x, u32 y) {
    if (!cursor_save) return;
    // Save region under cursor, clamped to screen bounds
    u32 w = CURSOR_W, h = CURSOR_H, sw = 0, sh = 0, scrw = 0, scrh = 0;
    fb_get_resolution(&scrw, &scrh);
    if (x + w > scrw) w = scrw - x;
    if (y + h > scrh) h = scrh - y;
    cursor_save_w = w; cursor_save_h = h;
    fb_copy_out(x, y, w, h, cursor_save);
    cursor_save_x = x; cursor_save_y = y;
    cursor_saved = 1;
    for (u32 yy = 0; yy < CURSOR_H; yy++) {
        for (u32 xx = 0; xx < CURSOR_W; xx++) {
            u8 v = CURSOR_PIXELS[yy][xx];
            if (v == 1) fb_draw_pixel(x + xx, y + yy, 0x00, 0x00, 0x00); // outline
            else if (v == 0) fb_draw_pixel(x + xx, y + yy, 0xFF, 0xFF, 0xFF); // fill
            // v==2 transparent: skip
        }
    }
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
    if (nx > (int)(scr_w - CURSOR_W)) nx = (int)(scr_w - CURSOR_W);
    if (ny > (int)(scr_h - CURSOR_H)) ny = (int)(scr_h - CURSOR_H);
    cursor_x = (u32)nx;
    cursor_y = (u32)ny;

    // Hover handling for bar menu: closes when cursor leaves menu/dropdown
    if (gui_bar_on_move(cursor_x, cursor_y)) {
        // Close of dropdown: redraw entire background + UI for a clean state
        // Clear last rect state (ignored) and fully rerender
        u32 rx, ry, rw, rh; (void)gui_bar_get_last_dropdown_rect(&rx, &ry, &rw, &rh);
        gui_cursor_undraw();
        gui_rerender_full();
        gui_cursor_draw(cursor_x, cursor_y);
    }
    // Close editor's menu on move outside
    if (cur_win == WIN_EDITOR) {
        if (gui_editor_on_move(cursor_x, cursor_y)) {
            gui_cursor_undraw();
            gui_rerender_full();
            gui_cursor_draw(cursor_x, cursor_y);
        }
    }

    int left_pressed = (buttons & 0x01) != 0;
    int left_was_pressed = (last_buttons & 0x01) != 0;
    int just_released = 0;
    int editor_click_consumed_local = 0;

    // Handle bar clicks (menu/dropdown)
    if (left_pressed && !left_was_pressed) {
        int clicked_window_id = -1;
        int action = gui_bar_on_click(cursor_x, cursor_y, &clicked_window_id);
        // After any menu click/toggle, re-render full background + UI
        { u32 rx, ry, rw, rh; (void)gui_bar_get_last_dropdown_rect(&rx, &ry, &rw, &rh); }
        gui_cursor_undraw();
        gui_rerender_full();
        gui_cursor_draw(cursor_x, cursor_y);
        if (action == 1) {
            // Open new terminal (single window supported): create if none, otherwise focus
            if (!window_open || cur_win != WIN_TERMINAL) {
                // If an editor exists, close it first
                if (window_open && cur_win == WIN_EDITOR) {
                    gui_editor_free();
                    if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
                    window_open = 0; cur_win = WIN_NONE;
                }
                // Default window size and position (below bar)
                win_w = scr_w / 2; win_h = scr_h / 2;
                if (win_w < 100) win_w = (scr_w > 120) ? 120 : scr_w - 20;
                if (win_h < 80)  win_h = (scr_h > 100) ? 100 : scr_h - 20;
                win_x = (scr_w - win_w) / 2; win_y = GUI_BAR_HEIGHT + 10;
                // Draw frame and register window
                gui_draw_window();
                terminal_win_id = gui_bar_register_window("Terminal");
                gui_bar_set_active_window(terminal_win_id);
                gui_bar_render();
                // Init terminal content area
                u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
                u32 content_x = win_x + 6;
                u32 content_y = win_y + 2 + title_h + 4;
                u32 content_w = (win_w > 12) ? (win_w - 12) : 0;
                u32 content_h = (win_h > title_h + 10) ? (win_h - title_h - 10) : 0;
                gui_term_init(content_x, content_y, content_w, content_h);
                gui_term_attach();
                gui_term_render_all();
                window_open = 1; cur_win = WIN_TERMINAL;
            } else {
                gui_bar_set_active_window(terminal_win_id);
                gui_bar_render();
            }
        } else if (action == 3) {
            // Open simple editor; if terminal exists, close it first
            if (window_open && cur_win == WIN_TERMINAL) {
                gui_term_detach(); gui_term_free();
                if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
                window_open = 0; cur_win = WIN_NONE;
            }
            // Create editor window
            // Default window size and position (below bar)
            win_w = scr_w / 2; win_h = scr_h / 2;
            if (win_w < 100) win_w = (scr_w > 120) ? 120 : scr_w - 20;
            if (win_h < 80)  win_h = (scr_h > 100) ? 100 : scr_h - 20;
            win_x = (scr_w - win_w) / 2; win_y = GUI_BAR_HEIGHT + 10;
            gui_draw_window();
            editor_win_id = gui_bar_register_window("Editor");
            gui_bar_set_active_window(editor_win_id);
            gui_bar_render();
            u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
            u32 content_x = win_x + 6;
            u32 content_y = win_y + 2 + title_h + 4;
            u32 content_w = (win_w > 12) ? (win_w - 12) : 0;
            u32 content_h = (win_h > title_h + 10) ? (win_h - title_h - 10) : 0;
            gui_editor_init(content_x, content_y, content_w, content_h);
            window_open = 1; cur_win = WIN_EDITOR;
            gui_rerender_full();
        } else if (action == 2 && clicked_window_id > 0) {
            // Focus a window tab: set active highlight. No Z-order change for now.
            if (clicked_window_id == terminal_win_id) { gui_bar_set_active_window(terminal_win_id); gui_bar_render(); }
            if (clicked_window_id == editor_win_id)   { gui_bar_set_active_window(editor_win_id);   gui_bar_render(); }
        }
        // Forward click to editor window UI (File menu)
        if (cur_win == WIN_EDITOR) {
            gui_cursor_undraw();
            if (gui_editor_on_click(cursor_x, cursor_y)) {
                editor_click_consumed_local = 1;
                gui_rerender_full();
            }
            gui_cursor_draw(cursor_x, cursor_y);
        }
    }

    // Start drag if left just pressed inside title bar (only when a window is open)
    if (window_open && !dragging && left_pressed && !left_was_pressed) {
        if (cur_win == WIN_EDITOR && editor_click_consumed_local) {
            last_buttons = buttons; return;
        }
        u32 tb_h = (win_h > 24) ? 24 : (win_h / 8);
        u32 tx1 = win_x + 2;
        u32 ty1 = win_y + 2;
        u32 tx2 = win_x + win_w - 2;
        u32 ty2 = ty1 + tb_h;
        // Close button hit (red square on right)
        u32 cb = (tb_h > 14) ? 12 : (tb_h - 4);
        u32 cx1 = win_x + win_w - 6 - cb, cy1 = win_y + 4, cx2 = cx1 + cb, cy2 = cy1 + cb;
        if (cursor_x >= cx1 && cursor_x < cx2 && cursor_y >= cy1 && cursor_y < cy2) {
            // Close current window: detach/free, clear area, unregister from bar
            gui_cursor_undraw();
            if (cur_win == WIN_TERMINAL) {
                gui_term_detach(); gui_term_free();
                if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
            } else if (cur_win == WIN_EDITOR) {
                gui_editor_free();
                if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
            }
            restore_bg_rect(win_x, win_y, win_w, win_h);
            gui_bar_render();
            window_open = 0; cur_win = WIN_NONE; dragging = 0; just_released = 1;
        } else if (cursor_x >= tx1 && cursor_x < tx2 && cursor_y >= ty1 && cursor_y < ty2) {
            dragging = 1;
            drag_off_x = cursor_x - win_x; drag_off_y = cursor_y - win_y;
        }
    }


    // Stop drag on release
    if (dragging && !left_pressed && left_was_pressed) {
        // End of drag; mark to render content once
        just_released = 1;
        dragging = 0;
    }

    // If dragging, move window and redraw
    int window_moved = 0;
    if (dragging) {
        int new_wx = (int)cursor_x - (int)drag_off_x;
        int new_wy = (int)cursor_y - (int)drag_off_y;
        // Dragging terminal
        if (new_wx < 0) new_wx = 0; if (new_wy < 0) new_wy = 0;
        if (new_wx > (int)(scr_w - win_w)) new_wx = (int)(scr_w - win_w);
        if (new_wy > (int)(scr_h - win_h)) new_wy = (int)(scr_h - win_h);
        if (win_x != (u32)new_wx || win_y != (u32)new_wy) { win_x = (u32)new_wx; win_y = (u32)new_wy; window_moved = 1; }
    }

    if (window_open && window_moved) {
        // Remove old cursor drawing so it doesn't interfere
        gui_cursor_undraw();
        // Clear old window area fully
        restore_bg_rect(old_wx, old_wy, win_w, win_h);
        // Draw new window frame
        gui_draw_window();
        gui_bar_render();
        // Update terminal anchor to new position
        u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
        u32 ncx = win_x + 6;
        u32 ncy = win_y + 2 + title_h + 4;
        if (cur_win == WIN_TERMINAL) gui_term_move(ncx, ncy);
        else if (cur_win == WIN_EDITOR) gui_editor_move(ncx, ncy);
        // Only redraw terminal content when drag stops to avoid heavy flicker
        if (just_released) {
            if (cur_win == WIN_TERMINAL) gui_term_render_all();
            else if (cur_win == WIN_EDITOR) {
                gui_editor_render_all();
                gui_editor_set_titlebar(win_x, win_y, win_w, title_h);
                gui_editor_draw_overlays();
            }
        } else {
            if (cur_win == WIN_EDITOR) {
                gui_editor_set_titlebar(win_x, win_y, win_w, title_h);
                gui_editor_draw_overlays();
            }
        }
        // Draw cursor at new position
        gui_cursor_draw(cursor_x, cursor_y);
    } else {
        // Just move the cursor with save/restore
        gui_cursor_undraw();
        gui_cursor_draw(cursor_x, cursor_y);
    }

    // If drag just ended but there was no delta in this packet, ensure full terminal redraw now
    if (window_open && just_released && !window_moved) {
        if (cur_win == WIN_TERMINAL) gui_term_render_all();
        else if (cur_win == WIN_EDITOR) gui_editor_render_all();
    }
    // no other window redraw required
    // no secondary window

    last_buttons = buttons;
}

static void gui_stop(void);

static void gui_key_handler(u8 scancode, int is_extended, int is_pressed) {
    (void)is_extended;
    if (!gui_active) return;
    if (is_pressed && scancode == US_ESC) {
        gui_stop();
    }
    // Forward keys depending on active window
    if (window_open && is_pressed) {
        if (cur_win == WIN_TERMINAL) {
            extern int tty_global_handle_key(u8 scancode, int is_extended);
            extern void cldramfs_shell_handle_input(void);
            if (tty_global_handle_key(scancode, is_extended)) {
                cldramfs_shell_handle_input();
            }
        } else if (cur_win == WIN_EDITOR) {
            gui_editor_handle_key(scancode, is_extended, is_pressed);
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
    if (cursor_x > scr_w - CURSOR_W) cursor_x = scr_w - CURSOR_W;
    if (cursor_y > scr_h - CURSOR_H) cursor_y = scr_h - CURSOR_H;

    // Prepare cursor save buffer
    fb_bpp = fb_get_bytespp();
    if (fb_bpp == 0) return;
    cursor_save = (u8*)kmalloc(CURSOR_W * CURSOR_H * fb_bpp);
    if (!cursor_save) return;

    // Try to load wallpaper from ramfs and clear background
    if (!gui_wallpaper_load("/wallpapers/default.bmp")) {
        // Fallback to legacy filename if default is missing
        (void)gui_wallpaper_load("/wallpapers/cldwallapper.bmp");
    }
    gui_clear_all();
    gui_draw_window();
    // Initialize and draw top bar
    gui_bar_init();
    gui_bar_render();
    // Terminal area: inside window, below title bar with margins
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    u32 content_x = win_x + 6;
    u32 content_y = win_y + 2 + title_h + 4;
    u32 content_w = (win_w > 12) ? (win_w - 12) : 0;
    u32 content_h = (win_h > title_h + 10) ? (win_h - title_h - 10) : 0;
    gui_term_init(content_x, content_y, content_w, content_h);
    gui_term_attach();
    // Register window with bar and set active
    terminal_win_id = gui_bar_register_window("Terminal");
    gui_bar_set_active_window(terminal_win_id);
    gui_bar_render();
    gui_cursor_draw(cursor_x, cursor_y);

    // Hook input callbacks and mark active
    ps2_mouse_set_callback(gui_mouse_cb);
    ps2_set_key_callback(gui_key_handler);
    gui_active = 1;
    window_open = 1;
    cur_win = WIN_TERMINAL;
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

    // Free active window backing store
    if (cur_win == WIN_TERMINAL) gui_term_free();
    else if (cur_win == WIN_EDITOR) gui_editor_free();
    // Unregister windows from bar
    if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
    if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
}
