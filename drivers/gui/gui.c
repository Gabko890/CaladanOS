#include <cldtypes.h>
#include <fb/fb_console.h>
#include <ps2.h>
#include <vgaio.h>
#include <shell_control.h>
#include <cldramfs/cldramfs.h>
#include <cldramfs/tty.h>
#include <kmalloc.h>
#include <lua_vm.h>
#include <string.h>
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
#define GUI_CONFIG_PATH     "/etc/gui/gui.lua"
#define GUI_DEFAULT_WALLPAPER "/usr/share/wallpapers/default.bmp"

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

static u8 gui_bg[3] = { 0x20, 0x20, 0x20 };
static char gui_config_wallpaper[256] = GUI_DEFAULT_WALLPAPER;
static char gui_active_wallpaper[256] = GUI_DEFAULT_WALLPAPER;
static int gui_temp_wallpaper = 0;
static int gui_config_loaded = 0;

static void gui_stop(void);
static void gui_key_handler(u8 scancode, int is_extended, int is_pressed);

static void copy_path(char *dst, const char *src) {
    if (!dst || !src) return;
    strncpy(dst, src, 255);
    dst[255] = '\0';
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_color(const char *s, u8 rgb[3]) {
    if (!s || !rgb) return 0;
    if (s[0] == '#') s++;
    for (int i = 0; i < 6; i++) {
        if (hex_nibble(s[i]) < 0) return 0;
    }
    if (s[6] != '\0') return 0;
    int r1 = hex_nibble(s[0]), r2 = hex_nibble(s[1]);
    int g1 = hex_nibble(s[2]), g2 = hex_nibble(s[3]);
    int b1 = hex_nibble(s[4]), b2 = hex_nibble(s[5]);
    rgb[0] = (u8)((r1 << 4) | r2);
    rgb[1] = (u8)((g1 << 4) | g2);
    rgb[2] = (u8)((b1 << 4) | b2);
    return 1;
}

static int ramfs_file_exists(const char *path) {
    Node *f = cldramfs_resolve_path_file(path, 0);
    return f && f->type == FILE_NODE && f->content;
}

static void gui_load_style(void) {
    gui_window_style_t window_style = {
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
    gui_bar_style_t bar_style = {
        { 0x25, 0x25, 0x28 },
        { 0x66, 0x66, 0x6A },
        { 0x40, 0x40, 0x44 },
    };
    gui_bg[0] = 0x20; gui_bg[1] = 0x20; gui_bg[2] = 0x20;

    char background[16] = "";
    char window[16] = "";
    char title[16] = "";
    char active_title[16] = "";
    char border[16] = "";
    char menu[16] = "";
    char popup[16] = "";
    char close_button[16] = "";
    char minimize_button[16] = "";
    char outline_light[16] = "";
    char outline_dark[16] = "";
    char bar_background[16] = "";
    char bar_active[16] = "";
    char bar_separator[16] = "";

    const char *keys[] = {
        "background",
        "window",
        "title",
        "active_title",
        "border",
        "menu",
        "popup",
        "close_button",
        "minimize_button",
        "outline_light",
        "outline_dark",
        "bar_background",
        "bar_active",
        "bar_separator",
    };
    char *values[] = {
        background,
        window,
        title,
        active_title,
        border,
        menu,
        popup,
        close_button,
        minimize_button,
        outline_light,
        outline_dark,
        bar_background,
        bar_active,
        bar_separator,
    };
    const u32 sizes[] = {
        sizeof(background),
        sizeof(window),
        sizeof(title),
        sizeof(active_title),
        sizeof(border),
        sizeof(menu),
        sizeof(popup),
        sizeof(close_button),
        sizeof(minimize_button),
        sizeof(outline_light),
        sizeof(outline_dark),
        sizeof(bar_background),
        sizeof(bar_active),
        sizeof(bar_separator),
    };

    (void)cld_luavm_read_config_strings(GUI_CONFIG_PATH, keys, values, sizes, 14);
    (void)parse_color(background, gui_bg);
    (void)parse_color(window, window_style.window);
    (void)parse_color(title, window_style.title);
    (void)parse_color(active_title, window_style.active_title);
    (void)parse_color(border, window_style.border);
    (void)parse_color(menu, window_style.menu);
    (void)parse_color(popup, window_style.popup);
    (void)parse_color(close_button, window_style.close_button);
    (void)parse_color(minimize_button, window_style.minimize_button);
    (void)parse_color(outline_light, window_style.outline_light);
    (void)parse_color(outline_dark, window_style.outline_dark);
    (void)parse_color(bar_background, bar_style.background);
    (void)parse_color(bar_active, bar_style.active);
    (void)parse_color(bar_separator, bar_style.separator);

    gui_window_set_style(&window_style);
    gui_bar_set_style(&bar_style);
}

static int gui_load_config(int preserve_temp_wallpaper) {
    copy_path(gui_config_wallpaper, GUI_DEFAULT_WALLPAPER);

    const char *keys[] = { "wallpaper" };
    char *values[] = { gui_config_wallpaper };
    const u32 sizes[] = { sizeof(gui_config_wallpaper) };
    int found = cld_luavm_read_config_strings(GUI_CONFIG_PATH, keys, values, sizes, 1);
    if (!preserve_temp_wallpaper) {
        copy_path(gui_active_wallpaper, gui_config_wallpaper);
        gui_temp_wallpaper = 0;
    }
    gui_load_style();
    gui_config_loaded = 1;
    return found > 0 || ramfs_file_exists(GUI_CONFIG_PATH);
}

static void draw_rect_rgb(u32 x, u32 y, u32 w, u32 h, const u8 rgb[3]) {
    fb_fill_rect_rgb(x, y, w, h, rgb[0], rgb[1], rgb[2]);
}

static void gui_clear_all(void) {
    if (gui_wallpaper_is_loaded()) gui_wallpaper_draw_fullscreen();
    else draw_rect_rgb(0, 0, scr_w, scr_h, gui_bg);
}

static void gui_clear_workspace(void) {
    if (scr_h <= GUI_BAR_HEIGHT) return;
    u32 y = GUI_BAR_HEIGHT;
    u32 h = scr_h - GUI_BAR_HEIGHT;
    if (gui_wallpaper_is_loaded()) gui_wallpaper_redraw_rect(0, y, scr_w, h);
    else draw_rect_rgb(0, y, scr_w, h, gui_bg);
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

int gui_reload_config(void) {
    int ok = gui_load_config(0);
    if (gui_active) {
        (void)gui_wallpaper_load(gui_active_wallpaper);
        gui_cursor_undraw();
        gui_render_desktop();
        gui_cursor_draw(cursor_x, cursor_y);
    }
    return ok;
}

int gui_reload_wallpaper(void) {
    if (!gui_config_loaded) (void)gui_load_config(gui_temp_wallpaper);
    if (!gui_active) return ramfs_file_exists(gui_active_wallpaper);
    int ok = gui_wallpaper_load(gui_active_wallpaper);
    gui_cursor_undraw();
    gui_render_desktop();
    gui_cursor_draw(cursor_x, cursor_y);
    return ok;
}

int gui_change_wallpaper(const char *path) {
    if (!path || !*path || !ramfs_file_exists(path)) return 0;
    copy_path(gui_active_wallpaper, path);
    gui_temp_wallpaper = 1;
    if (!gui_active) return 1;
    int ok = gui_wallpaper_load(gui_active_wallpaper);
    gui_cursor_undraw();
    gui_render_desktop();
    gui_cursor_draw(cursor_x, cursor_y);
    return ok;
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
    if (gui_active) return;
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
    (void)gui_load_config(0);
    (void)gui_wallpaper_load(gui_active_wallpaper);
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
    gui_temp_wallpaper = 0;
    copy_path(gui_active_wallpaper, gui_config_wallpaper);
}
