#ifndef GUI_BROWSER_H
#define GUI_BROWSER_H

#include <cldtypes.h>

void gui_browser_init(u32 px, u32 py, u32 pw, u32 ph);
void gui_browser_move(u32 px, u32 py);
void gui_browser_resize(u32 pw, u32 ph);
void gui_browser_render_all(void);
void gui_browser_free(void);
int gui_browser_on_click(u32 px, u32 py);
int gui_browser_on_right_click(u32 px, u32 py);
int gui_browser_on_move(u32 px, u32 py);
void gui_browser_handle_key(u8 scancode, int is_extended, int is_pressed);

#endif // GUI_BROWSER_H
