#ifndef GUI_VIEWER_H
#define GUI_VIEWER_H

#include <cldtypes.h>

// Initialize image viewer region inside given pixel rect (no image loaded yet)
void gui_viewer_init(u32 px, u32 py, u32 pw, u32 ph);
// Move viewer viewport to new pixel position (keeps content)
void gui_viewer_move(u32 px, u32 py);
void gui_viewer_resize(u32 pw, u32 ph);
// Redraw full viewer content
void gui_viewer_render_all(void);
// Free viewer resources
void gui_viewer_free(void);

// Titlebar integration and simple UI ("Open" button + dropdown)
void gui_viewer_set_titlebar(u32 win_x, u32 win_y, u32 win_w, u32 title_h);
void gui_viewer_draw_overlays(void);
// Return 1 if UI changed (open/close menu or selection). Sets internal flag if an image was loaded.
int gui_viewer_on_click(u32 px, u32 py);
int gui_viewer_on_move(u32 px, u32 py);
void gui_viewer_handle_key(u8 scancode, int is_extended, int is_pressed);
void gui_viewer_hide_open_button(void);

// Query/consume state
int gui_viewer_has_image(void);
void gui_viewer_get_image_dims(u32* w, u32* h);
// Returns 1 if a new image was opened since last call; resets the flag
int gui_viewer_consume_new_image_flag(void);

#endif // GUI_VIEWER_H
