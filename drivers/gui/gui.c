#include <cldtypes.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <vgaio.h>
#include <shell_control.h>
#include <cldramfs/tty.h>
#include <kmalloc.h>
#include "gui.h"
#include "term.h"
#include "editor.h"
#include "cursor_bitmap.h"
#include "bar.h"
#include "wallpaper.h"
#include "viewer.h"
#include "snake.h"
#include "calc.h"
#include "window.h"

#define GUI_FRAME_TITLE_H   24
#define GUI_FRAME_PAD_X     6
#define GUI_FRAME_PAD_Y     10

typedef enum {
    APP_TERMINAL = 0,
    APP_EDITOR,
    APP_VIEWER,
    APP_SNAKE,
    APP_CALC,
    APP_COUNT
} app_kind_t;

typedef struct {
    app_kind_t kind;
    gui_window_t *win;
    int initialized;
} gui_app_t;

static int gui_active = 0;
static u32 scr_w = 0, scr_h = 0;
static u32 cursor_x = 0, cursor_y = 0;
static u8 last_buttons = 0;
static int key_shift = 0;
static gui_app_t apps[APP_COUNT];

static u8 *cursor_save = 0;
static u32 cursor_save_x = 0, cursor_save_y = 0;
static u32 cursor_save_w = 0, cursor_save_h = 0;
static int cursor_saved = 0;
static u8 fb_bpp = 0;

static const u8 COL_BG[3] = { 0x20, 0x20, 0x20 };

static void gui_stop(void);
static void gui_key_handler(u8 scancode, int is_extended, int is_pressed);

static void draw_rect_rgb(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    fb_fill_rect_rgb(x, y, w, h, rgb[0], rgb[1], rgb[2]);
}

static void gui_clear_all(void) {
    if (gui_wallpaper_is_loaded()) gui_wallpaper_draw_fullscreen();
    else draw_rect_rgb(0, 0, scr_w, scr_h, COL_BG);
}

static void gui_clear_workspace(void) {
    if (scr_h <= GUI_BAR_HEIGHT) return;
    u32 y = GUI_BAR_HEIGHT;
    u32 h = scr_h - GUI_BAR_HEIGHT;
    if (gui_wallpaper_is_loaded()) gui_wallpaper_redraw_rect(0, y, scr_w, h);
    else draw_rect_rgb(0, y, scr_w, h, COL_BG);
}

static void gui_render_desktop(void) {
    gui_clear_all();
    gui_window_render_all();
    gui_bar_render();
}

static void gui_render_drag_preview(void) {
    gui_clear_workspace();
    gui_window_render_drag_preview_all();
}

static void gui_cursor_undraw(void) {
    if (cursor_saved && cursor_save) {
        if (cursor_save_w > 0 && cursor_save_h > 0) {
            fb_blit(cursor_save_x, cursor_save_y, cursor_save_w, cursor_save_h, cursor_save);
        }
        cursor_saved = 0;
    }
}

static void gui_cursor_draw(u32 x, u32 y) {
    if (!cursor_save) return;
    u32 w = CURSOR_W, h = CURSOR_H, sw = 0, sh = 0;
    fb_get_resolution(&sw, &sh);
    if (x >= sw || y >= sh) return;
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    cursor_save_w = w;
    cursor_save_h = h;
    cursor_save_x = x;
    cursor_save_y = y;
    fb_copy_out(x, y, w, h, cursor_save);
    cursor_saved = 1;
    for (u32 yy = 0; yy < CURSOR_H; yy++) {
        for (u32 xx = 0; xx < CURSOR_W; xx++) {
            u8 v = CURSOR_PIXELS[yy][xx];
            if (v == 1) fb_draw_pixel(x + xx, y + yy, 0x00, 0x00, 0x00);
            else if (v == 0) fb_draw_pixel(x + xx, y + yy, 0xFF, 0xFF, 0xFF);
        }
    }
}

static gui_app_t *app_from_window(gui_window_t *win) {
    if (!win) return 0;
    for (int i = 0; i < APP_COUNT; i++) {
        if (apps[i].win == win) return &apps[i];
    }
    return 0;
}

static void update_terminal_sink(void) {
    gui_app_t *app = app_from_window(gui_window_active());
    if (app && app->kind == APP_TERMINAL && app->initialized) gui_term_attach();
    else gui_term_detach();
}

static void app_render(gui_window_t *win, void *ctx) {
    gui_app_t *app = (gui_app_t*)ctx;
    if (!app || !app->initialized) return;
    if (app->kind == APP_TERMINAL) {
        gui_term_render_all();
    } else if (app->kind == APP_EDITOR) {
        gui_editor_set_titlebar(win->x, win->y, win->w, GUI_FRAME_TITLE_H);
        gui_editor_render_all();
        gui_editor_draw_overlays();
    } else if (app->kind == APP_VIEWER) {
        gui_viewer_set_titlebar(win->x, win->y, win->w, GUI_FRAME_TITLE_H);
        gui_viewer_render_all();
        gui_viewer_draw_overlays();
    } else if (app->kind == APP_SNAKE) {
        gui_snake_render_all();
    } else if (app->kind == APP_CALC) {
        gui_calc_render_all();
    }
}

static void app_move(gui_window_t *win, u32 content_x, u32 content_y, void *ctx) {
    (void)win;
    gui_app_t *app = (gui_app_t*)ctx;
    if (!app || !app->initialized) return;
    if (app->kind == APP_TERMINAL) gui_term_move(content_x, content_y);
    else if (app->kind == APP_EDITOR) gui_editor_move(content_x, content_y);
    else if (app->kind == APP_VIEWER) gui_viewer_move(content_x, content_y);
    else if (app->kind == APP_SNAKE) gui_snake_move(content_x, content_y);
    else if (app->kind == APP_CALC) gui_calc_move(content_x, content_y);
}

static void app_resize(gui_window_t *win, u32 content_w, u32 content_h, void *ctx) {
    gui_app_t *app = (gui_app_t*)ctx;
    u32 content_x = 0, content_y = 0, unused_w = 0, unused_h = 0;
    if (!app) return;
    gui_window_get_content_rect(win, &content_x, &content_y, &unused_w, &unused_h);
    if (!app->initialized) {
        if (app->kind == APP_TERMINAL) gui_term_init(content_x, content_y, content_w, content_h);
        else if (app->kind == APP_EDITOR) gui_editor_init(content_x, content_y, content_w, content_h);
        else if (app->kind == APP_VIEWER) gui_viewer_init(content_x, content_y, content_w, content_h);
        else if (app->kind == APP_SNAKE) gui_snake_init(content_x, content_y, content_w, content_h);
        else if (app->kind == APP_CALC) gui_calc_init(content_x, content_y, content_w, content_h);
        app->initialized = 1;
        return;
    }
    if (app->kind == APP_TERMINAL) gui_term_resize(content_w, content_h);
    else if (app->kind == APP_EDITOR) gui_editor_resize(content_w, content_h);
    else if (app->kind == APP_VIEWER) gui_viewer_resize(content_w, content_h);
    else if (app->kind == APP_SNAKE) gui_snake_resize(content_w, content_h);
    else if (app->kind == APP_CALC) gui_calc_resize(content_w, content_h);
}

static void app_close(gui_window_t *win, void *ctx) {
    (void)win;
    gui_app_t *app = (gui_app_t*)ctx;
    if (!app || !app->initialized) return;
    if (app->kind == APP_TERMINAL) {
        gui_term_detach();
        gui_term_free();
    } else if (app->kind == APP_EDITOR) {
        gui_editor_free();
    } else if (app->kind == APP_VIEWER) {
        gui_viewer_free();
    } else if (app->kind == APP_SNAKE) {
        gui_snake_free();
    } else if (app->kind == APP_CALC) {
        gui_calc_free();
    }
    app->initialized = 0;
    app->win = 0;
}

static void app_default_size(app_kind_t kind, u32 *w, u32 *h) {
    if (kind == APP_CALC) {
        *w = 220;
        *h = 240;
    } else {
        *w = scr_w / 2;
        *h = scr_h / 2;
    }
    if (*w < 140) *w = (scr_w > 160) ? 160 : scr_w - 10;
    if (*h < 100) *h = (scr_h > 130) ? 110 : scr_h - GUI_BAR_HEIGHT - 10;
    if (*w + 20 > scr_w) *w = scr_w > 20 ? scr_w - 20 : scr_w;
    if (*h + GUI_BAR_HEIGHT + 20 > scr_h) *h = scr_h > GUI_BAR_HEIGHT + 20 ? scr_h - GUI_BAR_HEIGHT - 20 : scr_h;
}

static gui_window_t *open_app(app_kind_t kind, const char *title, u32 flags) {
    gui_app_t *app = &apps[kind];
    if (app->win) {
        gui_window_restore(app->win);
        update_terminal_sink();
        return app->win;
    }

    u32 w = 0, h = 0;
    app_default_size(kind, &w, &h);
    u32 x = scr_w > w ? (scr_w - w) / 2 : 0;
    u32 y = GUI_BAR_HEIGHT + 10;
    app->kind = kind;
    app->initialized = 0;

    gui_window_callbacks_t cb;
    cb.render = app_render;
    cb.resize = app_resize;
    cb.move = app_move;
    cb.close = app_close;
    cb.minimize = 0;
    cb.menu = 0;
    cb.popup_submit = 0;
    cb.ctx = app;

    app->win = gui_window_create(title, x, y, w, h, flags, cb);
    if (app->win && kind == APP_TERMINAL) {
        gui_term_attach();
        tty_global_reset_line();
        tty_print_prompt();
    }
    update_terminal_sink();
    return app->win;
}

static void resize_viewer_to_image(gui_app_t *app) {
    if (!app || !app->win || app->kind != APP_VIEWER) return;
    u32 iw = 0, ih = 0;
    gui_viewer_get_image_dims(&iw, &ih);
    if (!iw || !ih) return;

    u32 max_cw = 600, max_ch = 400;
    u32 cw = iw, ch = ih;
    if (iw > max_cw || ih > max_ch) {
        if ((u64)iw * (u64)max_ch > (u64)ih * (u64)max_cw) {
            cw = max_cw;
            ch = (u32)(((u64)ih * (u64)max_cw) / (u64)iw);
            if (!ch) ch = 1;
        } else {
            ch = max_ch;
            cw = (u32)(((u64)iw * (u64)max_ch) / (u64)ih);
            if (!cw) cw = 1;
        }
    }

    u32 scr_max_w = scr_w > 20 ? scr_w - 20 : scr_w;
    u32 scr_max_h = scr_h > GUI_BAR_HEIGHT + 44 ? scr_h - GUI_BAR_HEIGHT - 44 : scr_h;
    if (cw > scr_max_w || ch > scr_max_h) {
        if ((u64)cw * (u64)scr_max_h > (u64)ch * (u64)scr_max_w) {
            u32 new_cw = scr_max_w;
            u32 new_ch = (u32)(((u64)ch * (u64)scr_max_w) / (u64)cw);
            cw = new_cw;
            ch = new_ch ? new_ch : 1;
        } else {
            u32 new_ch = scr_max_h;
            u32 new_cw = (u32)(((u64)cw * (u64)scr_max_h) / (u64)ch);
            ch = new_ch;
            cw = new_cw ? new_cw : 1;
        }
    }

    gui_window_resize(app->win, cw + GUI_FRAME_PAD_X * 2, ch + GUI_FRAME_TITLE_H + GUI_FRAME_PAD_Y);
    gui_window_move(app->win, scr_w > app->win->w ? (scr_w - app->win->w) / 2 : 0, GUI_BAR_HEIGHT + 10);
}

static int app_click(gui_window_t *win, u32 x, u32 y) {
    gui_app_t *app = app_from_window(win);
    if (!app || !app->initialized) return 0;
    if (app->kind == APP_EDITOR) {
        return gui_editor_on_click(x, y);
    }
    if (app->kind == APP_VIEWER) {
        int changed = gui_viewer_on_click(x, y);
        if (gui_viewer_consume_new_image_flag()) resize_viewer_to_image(app);
        return changed;
    }
    if (app->kind == APP_CALC) {
        return gui_calc_on_click(x, y);
    }
    return 0;
}

static int app_move_hover(gui_window_t *win, u32 x, u32 y) {
    gui_app_t *app = app_from_window(win);
    if (!app || !app->initialized) return 0;
    if (app->kind == APP_EDITOR) return gui_editor_on_move(x, y);
    if (app->kind == APP_VIEWER) return gui_viewer_on_move(x, y);
    return 0;
}

static void handle_bar_action(int action, int clicked_window_id) {
    if (action == 1) open_app(APP_TERMINAL, "Terminal", 0);
    else if (action == 3) open_app(APP_EDITOR, "Editor", 0);
    else if (action == 4) open_app(APP_VIEWER, "Viewer", 0);
    else if (action == 5) open_app(APP_SNAKE, "Snake", 0);
    else if (action == 6) open_app(APP_CALC, "Calculator", GUI_WINDOW_FIXED_SIZE);
    else if (action == 2 && clicked_window_id > 0) {
        gui_window_t *win = gui_window_by_id(clicked_window_id);
        if (win) gui_window_restore(win);
    }
    update_terminal_sink();
}

static void gui_mouse_cb(int dx, int dy, u8 buttons) {
    if (!gui_active) return;

    int nx = (int)cursor_x + dx;
    int ny = (int)cursor_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > (int)(scr_w - CURSOR_W)) nx = (int)(scr_w - CURSOR_W);
    if (ny > (int)(scr_h - CURSOR_H)) ny = (int)(scr_h - CURSOR_H);
    cursor_x = (u32)nx;
    cursor_y = (u32)ny;

    int left_pressed = (buttons & 0x01) != 0;
    int left_was_pressed = (last_buttons & 0x01) != 0;
    int redraw = 0;

    if (gui_bar_on_move(cursor_x, cursor_y)) redraw = 1;
    if (app_move_hover(gui_window_active(), cursor_x, cursor_y)) redraw = 1;

    if (left_pressed && !left_was_pressed) {
        if (cursor_y < GUI_BAR_HEIGHT || gui_bar_is_menu_open()) {
            int clicked_window_id = -1;
            int action = gui_bar_on_click(cursor_x, cursor_y, &clicked_window_id);
            if (action == 7) {
                gui_stop();
                last_buttons = buttons;
                return;
            }
            handle_bar_action(action, clicked_window_id);
            redraw = 1;
        } else {
            gui_window_t *win = gui_window_at(cursor_x, cursor_y);
            int app_consumed = app_click(win, cursor_x, cursor_y);
            if (win && app_consumed) {
                gui_window_focus(win);
                update_terminal_sink();
                redraw = 1;
            } else if (gui_window_mouse_down(cursor_x, cursor_y)) {
                update_terminal_sink();
                redraw = 1;
            }
        }
    } else if (left_pressed && left_was_pressed) {
        if (gui_window_mouse_drag(cursor_x, cursor_y)) redraw = 1;
    } else if (!left_pressed && left_was_pressed) {
        if (gui_window_mouse_up(cursor_x, cursor_y)) redraw = 1;
    }

    gui_cursor_undraw();
    if (redraw) {
        if (gui_window_is_dragging()) gui_render_drag_preview();
        else gui_render_desktop();
    }
    gui_cursor_draw(cursor_x, cursor_y);
    last_buttons = buttons;
}

void gui_open_snake(void) {
    if (!fb_console_present()) return;
    fb_get_resolution(&scr_w, &scr_h);
    open_app(APP_SNAKE, "Snake", 0);
    gui_cursor_undraw();
    gui_render_desktop();
    gui_cursor_draw(cursor_x, cursor_y);
}

void gui_close_terminal(void) {
    gui_app_t *app = &apps[APP_TERMINAL];
    if (app->win) {
        gui_window_destroy(app->win);
        update_terminal_sink();
        if (gui_active) {
            gui_cursor_undraw();
            gui_render_desktop();
            gui_cursor_draw(cursor_x, cursor_y);
        }
    }
}

void gui_restore_input(void) {
    if (gui_active) ps2_set_key_callback(gui_key_handler);
}

static void gui_key_handler(u8 scancode, int is_extended, int is_pressed) {
    if (scancode == 0x2A || scancode == 0x36) key_shift = is_pressed ? 1 : 0;
    if (!gui_active || !is_pressed) return;

    gui_window_t *win = gui_window_active();
    gui_app_t *app = app_from_window(win);
    if (!win || !app || !app->initialized) return;

    if (win->popup_open) {
        if (gui_window_popup_key(win, scancode, is_extended, key_shift)) {
            gui_cursor_undraw();
            gui_render_desktop();
            gui_cursor_draw(cursor_x, cursor_y);
        }
        return;
    }

    if (app->kind == APP_TERMINAL) {
        extern int tty_global_handle_key(u8 scancode, int is_extended);
        if (tty_global_handle_key(scancode, is_extended)) shell_schedule_gui_input();
    } else if (app->kind == APP_EDITOR) {
        gui_editor_handle_key(scancode, is_extended, is_pressed);
    } else if (app->kind == APP_VIEWER) {
        gui_viewer_handle_key(scancode, is_extended, is_pressed);
        if (gui_viewer_consume_new_image_flag()) resize_viewer_to_image(app);
    } else if (app->kind == APP_SNAKE) {
        gui_snake_handle_key(scancode, is_extended, is_pressed);
    } else if (app->kind == APP_CALC) {
        gui_calc_handle_key(scancode, is_extended, is_pressed);
    }
}

void gui_start(void) {
    if (!fb_console_present()) return;
    fb_get_resolution(&scr_w, &scr_h);
    if (!scr_w || !scr_h) return;

    shell_pause();
    cursor_x = scr_w / 2;
    cursor_y = scr_h / 2;
    if (cursor_x > scr_w - CURSOR_W) cursor_x = scr_w - CURSOR_W;
    if (cursor_y > scr_h - CURSOR_H) cursor_y = scr_h - CURSOR_H;

    fb_bpp = fb_get_bytespp();
    if (!fb_bpp) {
        shell_resume();
        return;
    }
    cursor_save = (u8*)kmalloc(CURSOR_W * CURSOR_H * fb_bpp);
    if (!cursor_save) {
        shell_resume();
        return;
    }

    for (int i = 0; i < APP_COUNT; i++) {
        apps[i].kind = (app_kind_t)i;
        apps[i].win = 0;
        apps[i].initialized = 0;
    }

    vga_clear_screen();
    (void)gui_wallpaper_load("/usr/share/wallpapers/default.bmp");
    gui_window_manager_init();
    gui_bar_init();
    gui_render_desktop();
    gui_cursor_draw(cursor_x, cursor_y);

    ps2_mouse_set_callback(gui_mouse_cb);
    ps2_set_key_callback(gui_key_handler);
    gui_active = 1;
    last_buttons = 0;
    key_shift = 0;
}

static void gui_stop(void) {
    if (!gui_active) return;
    gui_active = 0;
    ps2_mouse_set_callback(0);
    ps2_set_key_callback(0);
    gui_term_detach();
    gui_window_destroy_all();
    vga_clear_screen();
    shell_resume();
    if (cursor_save) {
        kfree(cursor_save);
        cursor_save = 0;
        cursor_saved = 0;
    }
}
