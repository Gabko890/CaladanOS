#ifndef GUI_CALC_H
#define GUI_CALC_H

#include <cldtypes.h>

// Initialize calculator region inside given pixel rect
void gui_calc_init(u32 px, u32 py, u32 pw, u32 ph);
// Move calculator viewport to new pixel position (keeps content)
void gui_calc_move(u32 px, u32 py);
// Redraw full calculator content
void gui_calc_render_all(void);
// Handle mouse click; returns 1 if state changed and needs rerender
int gui_calc_on_click(u32 px, u32 py);
// Optional: handle keyboard keys for digits/operators
void gui_calc_handle_key(u8 scancode, int is_extended, int is_pressed);
// Free any resources
void gui_calc_free(void);

#endif // GUI_CALC_H

