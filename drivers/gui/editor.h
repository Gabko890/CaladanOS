#ifndef GUI_EDITOR_H
#define GUI_EDITOR_H

#include <cldtypes.h>

// Initialize editor region inside given pixel rect
void gui_editor_init(u32 px, u32 py, u32 pw, u32 ph);
// Move editor viewport to new pixel position (keeps content)
void gui_editor_move(u32 px, u32 py);
// Redraw full editor content
void gui_editor_render_all(void);
// Handle a key event (press/release)
void gui_editor_handle_key(u8 scancode, int is_extended, int is_pressed);
// Free editor backing store
void gui_editor_free(void);
// Mouse interactions (global pixel coords). Return 1 if UI state changed.
int gui_editor_on_click(u32 px, u32 py);
int gui_editor_on_move(u32 px, u32 py);
// Provide titlebar geometry so editor can place its button/menu
void gui_editor_set_titlebar(u32 win_x, u32 win_y, u32 win_w, u32 title_h);
// Draw overlays (titlebar File button, dropdown, modals)
void gui_editor_draw_overlays(void);

#endif // GUI_EDITOR_H
