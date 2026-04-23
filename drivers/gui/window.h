#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include <cldtypes.h>

#define GUI_WINDOW_MAX          8
#define GUI_WINDOW_TITLE_MAX    48
#define GUI_WINDOW_MENU_MAX     6
#define GUI_WINDOW_MENU_TEXT_MAX 24
#define GUI_WINDOW_POPUP_TEXT_MAX 128

#define GUI_WINDOW_FIXED_SIZE   0x01
#define GUI_WINDOW_NO_MINIMIZE  0x02
#define GUI_WINDOW_NO_CLOSE     0x04

typedef struct gui_window gui_window_t;

typedef struct {
    u8 window[3];
    u8 title[3];
    u8 active_title[3];
    u8 border[3];
    u8 menu[3];
    u8 popup[3];
    u8 close_button[3];
    u8 minimize_button[3];
    u8 outline_light[3];
    u8 outline_dark[3];
} gui_window_style_t;

typedef void (*gui_window_render_fn)(gui_window_t *win, void *ctx);
typedef void (*gui_window_resize_fn)(gui_window_t *win, u32 content_w, u32 content_h, void *ctx);
typedef void (*gui_window_move_fn)(gui_window_t *win, u32 content_x, u32 content_y, void *ctx);
typedef void (*gui_window_close_fn)(gui_window_t *win, void *ctx);
typedef void (*gui_window_minimize_fn)(gui_window_t *win, void *ctx);
typedef void (*gui_window_menu_fn)(gui_window_t *win, int item, void *ctx);
typedef void (*gui_window_popup_fn)(gui_window_t *win, const char *text, void *ctx);

typedef struct {
    gui_window_render_fn render;
    gui_window_resize_fn resize;
    gui_window_move_fn move;
    gui_window_close_fn close;
    gui_window_minimize_fn minimize;
    gui_window_menu_fn menu;
    gui_window_popup_fn popup_submit;
    void *ctx;
} gui_window_callbacks_t;

struct gui_window {
    int id;
    int used;
    int minimized;
    u32 flags;
    u32 x;
    u32 y;
    u32 w;
    u32 h;
    u32 min_w;
    u32 min_h;
    u32 title_left_inset;
    char title[GUI_WINDOW_TITLE_MAX];
    char menu_items[GUI_WINDOW_MENU_MAX][GUI_WINDOW_MENU_TEXT_MAX];
    int menu_count;
    int menu_open;
    int popup_open;
    char popup_title[GUI_WINDOW_MENU_TEXT_MAX];
    char popup_buf[GUI_WINDOW_POPUP_TEXT_MAX];
    u32 popup_len;
    gui_window_callbacks_t cb;
};

void gui_window_manager_init(void);
void gui_window_set_style(const gui_window_style_t *style);
gui_window_t* gui_window_create(const char *title, u32 x, u32 y, u32 w, u32 h, u32 flags, gui_window_callbacks_t cb);
void gui_window_destroy(gui_window_t *win);
void gui_window_destroy_all(void);
void gui_window_set_title(gui_window_t *win, const char *title);
void gui_window_reserve_title_left(gui_window_t *win, const char *label);
void gui_window_set_min_size(gui_window_t *win, u32 min_w, u32 min_h);
void gui_window_add_menu_item(gui_window_t *win, const char *label);
void gui_window_open_popup(gui_window_t *win, const char *title, const char *initial);
int gui_window_popup_key(gui_window_t *win, u8 scancode, int is_extended, int shift);

gui_window_t* gui_window_active(void);
void gui_window_focus(gui_window_t *win);
gui_window_t* gui_window_by_id(int id);
gui_window_t* gui_window_at(u32 x, u32 y);

void gui_window_get_content_rect(gui_window_t *win, u32 *x, u32 *y, u32 *w, u32 *h);
void gui_window_move(gui_window_t *win, u32 x, u32 y);
void gui_window_resize(gui_window_t *win, u32 w, u32 h);
void gui_window_minimize(gui_window_t *win);
void gui_window_restore(gui_window_t *win);

void gui_window_render_frame(gui_window_t *win);
void gui_window_render(gui_window_t *win);
void gui_window_render_all(void);
void gui_window_render_drag_preview_all(void);
int gui_window_is_dragging(void);

// Returns 1 if handled and the caller should redraw.
int gui_window_mouse_down(u32 x, u32 y);
int gui_window_mouse_drag(u32 x, u32 y);
int gui_window_mouse_up(u32 x, u32 y);

#endif // GUI_WINDOW_H
