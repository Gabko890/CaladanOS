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

#endif // GUI_EDITOR_H

