#include <cldtypes.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <vgaio.h>
#include <shell_control.h>
#include <kmalloc.h>
#include "gui.h"
#include "term.h"
#include "editor.h"
#include "cursor_bitmap.h"
#include "bar.h"
#include "wallpaper.h"
#include "viewer.h"
#include "snake.h"

// Simple GUI state
static int gui_active = 0;
static int terminal_win_id = -1;
static int editor_win_id = -1;
static int viewer_win_id = -1;
static int snake_win_id = -1;
static int window_open = 0;
typedef enum { WIN_NONE = 0, WIN_TERMINAL = 1, WIN_EDITOR = 2, WIN_VIEWER = 3, WIN_SNAKE = 4 } win_kind_t;
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
// static const u8 COL_CURSOR[3] = { 0x00, 0x00, 0x00 }; // unused

// Forward declaration for local stop helper
static void gui_stop(void);

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

// Redraw only the current window area and its content/overlays (not the top bar)
static void gui_redraw_window_only(void) {
    if (!window_open) return;
    gui_draw_window();
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    u32 ncx = win_x + 6;
    u32 ncy = win_y + 2 + title_h + 4;
    if (cur_win == WIN_TERMINAL) {
        gui_term_move(ncx, ncy);
        gui_term_render_all();
    } else if (cur_win == WIN_EDITOR) {
        gui_editor_move(ncx, ncy);
        gui_editor_render_all();
        gui_editor_set_titlebar(win_x, win_y, win_w, title_h);
        gui_editor_draw_overlays();
    } else if (cur_win == WIN_VIEWER) {
        gui_viewer_move(ncx, ncy);
        gui_viewer_render_all();
        gui_viewer_set_titlebar(win_x, win_y, win_w, title_h);
        gui_viewer_draw_overlays();
    } else if (cur_win == WIN_SNAKE) {
        gui_snake_move(ncx, ncy);
        gui_snake_render_all();
    }
}

// (no window-only redraw; rely on full rerender for simplicity)

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
        } else if (cur_win == WIN_VIEWER) {
            gui_viewer_render_all();
            gui_viewer_set_titlebar(win_x, win_y, win_w, title_h);
            gui_viewer_draw_overlays();
        } else if (cur_win == WIN_SNAKE) {
            gui_snake_render_all();
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
    u32 w = CURSOR_W, h = CURSOR_H, scrw = 0, scrh = 0;
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
        // Bar repainted; restore dropdown area to background and refresh window underneath
        u32 rx, ry, rw, rh;
        int had_rect = gui_bar_get_last_dropdown_rect(&rx, &ry, &rw, &rh);
        gui_cursor_undraw();
        if (had_rect) restore_bg_rect(rx, ry, rw, rh);
        gui_redraw_window_only();
        gui_cursor_draw(cursor_x, cursor_y);
    }
    // Close editor's menu on move outside
    if (cur_win == WIN_EDITOR) {
        if (gui_editor_on_move(cursor_x, cursor_y)) {
            gui_cursor_undraw();
            gui_redraw_window_only();
            gui_cursor_draw(cursor_x, cursor_y);
        }
    } else if (cur_win == WIN_VIEWER) {
        if (gui_viewer_on_move(cursor_x, cursor_y)) {
            gui_cursor_undraw();
            gui_redraw_window_only();
            gui_cursor_draw(cursor_x, cursor_y);
        }
    }

    int left_pressed = (buttons & 0x01) != 0;
    int left_was_pressed = (last_buttons & 0x01) != 0;
    int just_released = 0;
    int editor_click_consumed_local = 0;
    int viewer_click_consumed_local = 0;

    // Handle bar clicks (menu/dropdown) only if clicking in bar area or when bar menu is open
    if (left_pressed && !left_was_pressed) {
        int clicked_window_id = -1;
        int action = 0;
        if (cursor_y < GUI_BAR_HEIGHT || gui_bar_is_menu_open()) {
            // Track state and rect to restore if menu closes
            int was_open = gui_bar_is_menu_open();
            u32 prev_rx=0, prev_ry=0, prev_rw=0, prev_rh=0; (void)gui_bar_get_current_dropdown_rect(&prev_rx, &prev_ry, &prev_rw, &prev_rh);
            gui_cursor_undraw();
            action = gui_bar_on_click(cursor_x, cursor_y, &clicked_window_id);
            int is_open = gui_bar_is_menu_open();
            if (was_open && !is_open && prev_rw && prev_rh) {
                restore_bg_rect(prev_rx, prev_ry, prev_rw, prev_rh);
                gui_redraw_window_only();
            }
            // Fallback for programmatic close
            u32 rx, ry, rw, rh; int closed = gui_bar_get_last_dropdown_rect(&rx, &ry, &rw, &rh);
            if (closed) { restore_bg_rect(rx, ry, rw, rh); gui_redraw_window_only(); }
            gui_cursor_draw(cursor_x, cursor_y);
        }
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
        } else if (action == 4) {
            // Open image viewer; close other window if present
            if (window_open && cur_win == WIN_TERMINAL) {
                gui_term_detach(); gui_term_free();
                if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
                window_open = 0; cur_win = WIN_NONE;
            } else if (window_open && cur_win == WIN_EDITOR) {
                gui_editor_free();
                if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
                window_open = 0; cur_win = WIN_NONE;
            }
            // Create viewer window
            win_w = scr_w / 2; win_h = scr_h / 2;
            if (win_w < 100) win_w = (scr_w > 120) ? 120 : scr_w - 20;
            if (win_h < 80)  win_h = (scr_h > 100) ? 100 : scr_h - 20;
            win_x = (scr_w - win_w) / 2; win_y = GUI_BAR_HEIGHT + 10;
            gui_draw_window();
            viewer_win_id = gui_bar_register_window("Viewer");
            gui_bar_set_active_window(viewer_win_id);
            gui_bar_render();
            u32 title_h2 = (win_h > 24) ? 24 : (win_h / 8);
            u32 content_x2 = win_x + 6;
            u32 content_y2 = win_y + 2 + title_h2 + 4;
            u32 content_w2 = (win_w > 12) ? (win_w - 12) : 0;
            u32 content_h2 = (win_h > title_h2 + 10) ? (win_h - title_h2 - 10) : 0;
            gui_viewer_init(content_x2, content_y2, content_w2, content_h2);
            window_open = 1; cur_win = WIN_VIEWER;
            gui_rerender_full();
        } else if (action == 5) {
            // Open Snake; close other window if present
            if (window_open && cur_win == WIN_TERMINAL) {
                gui_term_detach(); gui_term_free();
                if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
                window_open = 0; cur_win = WIN_NONE;
            } else if (window_open && cur_win == WIN_EDITOR) {
                gui_editor_free();
                if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
                window_open = 0; cur_win = WIN_NONE;
            } else if (window_open && cur_win == WIN_VIEWER) {
                gui_viewer_free();
                if (viewer_win_id > 0) { gui_bar_unregister_window(viewer_win_id); viewer_win_id = -1; }
                window_open = 0; cur_win = WIN_NONE;
            }
            // Create snake window
            win_w = scr_w / 2; win_h = scr_h / 2;
            if (win_w < 100) win_w = (scr_w > 120) ? 120 : scr_w - 20;
            if (win_h < 80)  win_h = (scr_h > 100) ? 100 : scr_h - 20;
            win_x = (scr_w - win_w) / 2; win_y = GUI_BAR_HEIGHT + 10;
            gui_draw_window();
            snake_win_id = gui_bar_register_window("Snake");
            gui_bar_set_active_window(snake_win_id);
            gui_bar_render();
            u32 title_h3 = (win_h > 24) ? 24 : (win_h / 8);
            u32 content_x3 = win_x + 6;
            u32 content_y3 = win_y + 2 + title_h3 + 4;
            u32 content_w3 = (win_w > 12) ? (win_w - 12) : 0;
            u32 content_h3 = (win_h > title_h3 + 10) ? (win_h - title_h3 - 10) : 0;
            gui_snake_init(content_x3, content_y3, content_w3, content_h3);
            window_open = 1; cur_win = WIN_SNAKE;
            gui_rerender_full();
        } else if (action == 6) {
            // Exit GUI via menu
            gui_stop();
            return; // stop processing further
        } else if (action == 2 && clicked_window_id > 0) {
            // Focus a window tab: set active highlight. No Z-order change for now.
            if (clicked_window_id == terminal_win_id) { gui_bar_set_active_window(terminal_win_id); gui_bar_render(); }
            if (clicked_window_id == editor_win_id)   { gui_bar_set_active_window(editor_win_id);   gui_bar_render(); }
            if (clicked_window_id == viewer_win_id)   { gui_bar_set_active_window(viewer_win_id);   gui_bar_render(); }
            if (clicked_window_id == snake_win_id)    { gui_bar_set_active_window(snake_win_id);    gui_bar_render(); }
        }
        // Forward click to editor/viewer window UI (File/Open menu)
        if (cur_win == WIN_EDITOR) {
            gui_cursor_undraw();
            if (gui_editor_on_click(cursor_x, cursor_y)) { editor_click_consumed_local = 1; gui_rerender_full(); }
            gui_cursor_draw(cursor_x, cursor_y);
        } else if (cur_win == WIN_VIEWER) {
            gui_cursor_undraw();
            if (gui_viewer_on_click(cursor_x, cursor_y)) {
                gui_rerender_full();
                viewer_click_consumed_local = 1;
                if (gui_viewer_consume_new_image_flag()) {
                    u32 iw = 0, ih = 0; gui_viewer_get_image_dims(&iw, &ih);
                    if (iw && ih) {
                        u32 sw = 0, sh = 0; fb_get_resolution(&sw, &sh);
                        u32 max_cw = 600, max_ch = 400; // viewer content max
                        // Downscale to fit box 600x400, preserving aspect; never upscale
                        u32 cw = iw, ch = ih;
                        if (iw > max_cw || ih > max_ch) {
                            // Compare iw/ih with max_cw/max_ch without division
                            if ((u64)iw * (u64)max_ch > (u64)ih * (u64)max_cw) {
                                // Width-bound
                                cw = max_cw;
                                ch = (u32)(((u64)ih * (u64)max_cw) / (u64)iw);
                                if (ch == 0) ch = 1;
                            } else {
                                // Height-bound
                                ch = max_ch;
                                cw = (u32)(((u64)iw * (u64)max_ch) / (u64)ih);
                                if (cw == 0) cw = 1;
                            }
                        }
                        // Screen safety clamp while preserving aspect (rarely needed)
                        u32 scr_max_w = (sw > 20) ? (sw - 20) : sw;
                        u32 scr_max_h = (sh > GUI_BAR_HEIGHT + 44) ? (sh - GUI_BAR_HEIGHT - 44) : (sh > 10 ? sh - 10 : sh);
                        if (cw > scr_max_w || ch > scr_max_h) {
                            if ((u64)cw * (u64)scr_max_h > (u64)ch * (u64)scr_max_w) {
                                u32 new_cw = scr_max_w;
                                u32 new_ch = (u32)(((u64)ch * (u64)scr_max_w) / (u64)cw);
                                cw = new_cw; ch = (new_ch ? new_ch : 1);
                            } else {
                                u32 new_ch = scr_max_h;
                                u32 new_cw = (u32)(((u64)cw * (u64)scr_max_h) / (u64)ch);
                                ch = new_ch; cw = (new_cw ? new_cw : 1);
                            }
                        }
                        // Derive window size from content size
                        u32 title_h2 = 24;
                        win_w = cw + 12;
                        win_h = ch + title_h2 + 10;
                        win_x = (sw > win_w) ? ((sw - win_w) / 2) : 0;
                        win_y = GUI_BAR_HEIGHT + 10;
                        // Redraw with new geometry
                        gui_draw_window();
                        u32 content_x2 = win_x + 6;
                        u32 content_y2 = win_y + 2 + title_h2 + 4;
                        gui_viewer_resize(cw, ch);
                        gui_viewer_move(content_x2, content_y2);
                        gui_rerender_full();
                    }
                }
            }
            gui_cursor_draw(cursor_x, cursor_y);
        }
    }

    // Start drag if left just pressed inside title bar (only when a window is open)
    if (window_open && !dragging && left_pressed && !left_was_pressed) {
        if ((cur_win == WIN_EDITOR && editor_click_consumed_local) ||
            (cur_win == WIN_VIEWER && viewer_click_consumed_local)) {
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
            } else if (cur_win == WIN_VIEWER) {
                gui_viewer_free();
                if (viewer_win_id > 0) { gui_bar_unregister_window(viewer_win_id); viewer_win_id = -1; }
            } else if (cur_win == WIN_SNAKE) {
                gui_snake_free();
                if (snake_win_id > 0) { gui_bar_unregister_window(snake_win_id); snake_win_id = -1; }
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
        if (new_wx < 0) new_wx = 0;
        if (new_wy < 0) new_wy = 0;
        if (new_wx > (int)(scr_w - win_w)) new_wx = (int)(scr_w - win_w);
        // Prevent overlapping the top bar
        int min_y = (int)GUI_BAR_HEIGHT + 2; // keep a small gap for window border
        if (new_wy < min_y) new_wy = min_y;
        if (new_wy > (int)(scr_h - win_h)) new_wy = (int)(scr_h - win_h);
        if (win_x != (u32)new_wx || win_y != (u32)new_wy) { win_x = (u32)new_wx; win_y = (u32)new_wy; window_moved = 1; }
    }

    if (window_open && window_moved) {
        // Remove old cursor drawing so it doesn't interfere
        gui_cursor_undraw();
        // Clear old window area fully
        restore_bg_rect(old_wx, old_wy, win_w, win_h);
        // Draw new window frame (bar unchanged while dragging)
        gui_draw_window();
        // Update terminal anchor to new position
        u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
        u32 ncx = win_x + 6;
        u32 ncy = win_y + 2 + title_h + 4;
        if (cur_win == WIN_TERMINAL) gui_term_move(ncx, ncy);
        else if (cur_win == WIN_EDITOR) gui_editor_move(ncx, ncy);
        else if (cur_win == WIN_VIEWER) gui_viewer_move(ncx, ncy);
        else if (cur_win == WIN_SNAKE) gui_snake_move(ncx, ncy);
        // Only redraw terminal content when drag stops to avoid heavy flicker
        if (just_released) {
            if (cur_win == WIN_TERMINAL) gui_term_render_all();
            else if (cur_win == WIN_EDITOR) {
                gui_editor_render_all();
                gui_editor_set_titlebar(win_x, win_y, win_w, title_h);
                gui_editor_draw_overlays();
            } else if (cur_win == WIN_VIEWER) {
                gui_viewer_render_all();
                gui_viewer_set_titlebar(win_x, win_y, win_w, title_h);
                gui_viewer_draw_overlays();
            } else if (cur_win == WIN_SNAKE) {
                gui_snake_render_all();
            }
        } else {
            if (cur_win == WIN_EDITOR) {
                gui_editor_set_titlebar(win_x, win_y, win_w, title_h);
                gui_editor_draw_overlays();
            } else if (cur_win == WIN_VIEWER) {
                gui_viewer_set_titlebar(win_x, win_y, win_w, title_h);
                gui_viewer_draw_overlays();
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
        else if (cur_win == WIN_VIEWER) gui_viewer_render_all();
        else if (cur_win == WIN_SNAKE) gui_snake_render_all();
    }
    // no other window redraw required
    // no secondary window

    last_buttons = buttons;
}

void gui_open_snake(void) {
    if (!fb_console_present()) return;
    // Close any existing window
    if (window_open) {
        if (cur_win == WIN_TERMINAL) {
            gui_term_detach(); gui_term_free();
            if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
        } else if (cur_win == WIN_EDITOR) {
            gui_editor_free();
            if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
        } else if (cur_win == WIN_VIEWER) {
            gui_viewer_free();
            if (viewer_win_id > 0) { gui_bar_unregister_window(viewer_win_id); viewer_win_id = -1; }
        } else if (cur_win == WIN_SNAKE) {
            gui_snake_free();
            if (snake_win_id > 0) { gui_bar_unregister_window(snake_win_id); snake_win_id = -1; }
        }
        window_open = 0; cur_win = WIN_NONE;
    }
    // Create snake window centered
    fb_get_resolution(&scr_w, &scr_h);
    win_w = scr_w / 2; win_h = scr_h / 2;
    if (win_w < 100) win_w = (scr_w > 120) ? 120 : scr_w - 20;
    if (win_h < 80)  win_h = (scr_h > 100) ? 100 : scr_h - 20;
    win_x = (scr_w - win_w) / 2; win_y = GUI_BAR_HEIGHT + 10;
    gui_draw_window();
    if (snake_win_id > 0) { gui_bar_unregister_window(snake_win_id); snake_win_id = -1; }
    snake_win_id = gui_bar_register_window("Snake");
    gui_bar_set_active_window(snake_win_id);
    gui_bar_render();
    u32 title_h = (win_h > 24) ? 24 : (win_h / 8);
    u32 cx = win_x + 6;
    u32 cy = win_y + 2 + title_h + 4;
    u32 cw = (win_w > 12) ? (win_w - 12) : 0;
    u32 ch = (win_h > title_h + 10) ? (win_h - title_h - 10) : 0;
    gui_snake_init(cx, cy, cw, ch);
    window_open = 1; cur_win = WIN_SNAKE;
    gui_rerender_full();
}

static void gui_key_handler(u8 scancode, int is_extended, int is_pressed) {
    (void)is_extended;
    if (!gui_active) return;
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
        } else if (cur_win == WIN_VIEWER) {
            gui_viewer_handle_key(scancode, is_extended, is_pressed);
            if (gui_viewer_consume_new_image_flag()) {
                u32 iw = 0, ih = 0; gui_viewer_get_image_dims(&iw, &ih);
                if (iw && ih) {
                    u32 sw = 0, sh = 0; fb_get_resolution(&sw, &sh);
                    u32 max_cw = 600, max_ch = 400;
                    u32 cw = iw, ch = ih;
                    if (iw > max_cw || ih > max_ch) {
                        if ((u64)iw * (u64)max_ch > (u64)ih * (u64)max_cw) {
                            cw = max_cw;
                            ch = (u32)(((u64)ih * (u64)max_cw) / (u64)iw);
                            if (ch == 0) ch = 1;
                        } else {
                            ch = max_ch;
                            cw = (u32)(((u64)iw * (u64)max_ch) / (u64)ih);
                            if (cw == 0) cw = 1;
                        }
                    }
                    // screen clamp preserving aspect
                    u32 scr_max_w = (sw > 20) ? (sw - 20) : sw;
                    u32 scr_max_h = (sh > GUI_BAR_HEIGHT + 44) ? (sh - GUI_BAR_HEIGHT - 44) : (sh > 10 ? sh - 10 : sh);
                    if (cw > scr_max_w || ch > scr_max_h) {
                        if ((u64)cw * (u64)scr_max_h > (u64)ch * (u64)scr_max_w) {
                            u32 new_cw = scr_max_w; u32 new_ch = (u32)(((u64)ch * (u64)scr_max_w) / (u64)cw);
                            cw = new_cw; ch = (new_ch ? new_ch : 1);
                        } else {
                            u32 new_ch = scr_max_h; u32 new_cw = (u32)(((u64)cw * (u64)scr_max_h) / (u64)ch);
                            ch = new_ch; cw = (new_cw ? new_cw : 1);
                        }
                    }
                    u32 title_h2 = 24;
                    win_w = cw + 12;
                    win_h = ch + title_h2 + 10;
                    win_x = (sw > win_w) ? ((sw - win_w) / 2) : 0;
                    win_y = GUI_BAR_HEIGHT + 10;
                    gui_draw_window();
                    u32 content_x2 = win_x + 6;
                    u32 content_y2 = win_y + 2 + title_h2 + 4;
                    gui_viewer_resize(cw, ch);
                    gui_viewer_move(content_x2, content_y2);
                    gui_rerender_full();
                }
            }
        } else if (cur_win == WIN_SNAKE) {
            gui_snake_handle_key(scancode, is_extended, is_pressed);
        }
    }
}

void gui_start(void) {
    if (!fb_console_present()) {
        return; // No framebuffer; GUI not available
    }

    fb_get_resolution(&scr_w, &scr_h);
    if (scr_w == 0 || scr_h == 0) return;

    // Pause shell I/O so it doesn't draw over the GUI background
    shell_pause();

    // No window opened by default; just draw wallpaper and the top bar.
    win_w = win_h = 0;
    win_x = win_y = 0;

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

    // Clear any existing text framebuffer content first
    vga_clear_screen();

    // Try to load wallpaper from ramfs and clear background
    if (!gui_wallpaper_load("/wallpapers/default.bmp")) {
        // Fallback to legacy filename if default is missing
        (void)gui_wallpaper_load("/wallpapers/cldwallapper.bmp");
    }
    gui_clear_all();
    // Initialize and draw top bar
    gui_bar_init();
    gui_bar_render();
    gui_cursor_draw(cursor_x, cursor_y);

    // Hook input callbacks and mark active
    ps2_mouse_set_callback(gui_mouse_cb);
    ps2_set_key_callback(gui_key_handler);
    gui_active = 1;
    window_open = 0;
    cur_win = WIN_NONE;
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
    else if (cur_win == WIN_VIEWER) gui_viewer_free();
    else if (cur_win == WIN_SNAKE) gui_snake_free();
    // Unregister windows from bar
    if (editor_win_id > 0) { gui_bar_unregister_window(editor_win_id); editor_win_id = -1; }
    if (terminal_win_id > 0) { gui_bar_unregister_window(terminal_win_id); terminal_win_id = -1; }
    if (viewer_win_id > 0) { gui_bar_unregister_window(viewer_win_id); viewer_win_id = -1; }
    if (snake_win_id > 0) { gui_bar_unregister_window(snake_win_id); snake_win_id = -1; }
}
