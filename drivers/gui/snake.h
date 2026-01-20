#ifndef GUI_SNAKE_H
#define GUI_SNAKE_H

#include <cldtypes.h>

// Initialize snake game in the given pixel rect
void gui_snake_init(u32 px, u32 py, u32 pw, u32 ph);
// Move anchor (pixel origin) when window is dragged
void gui_snake_move(u32 px, u32 py);
// Render entire game area
void gui_snake_render_all(void);
// Handle key (arrow keys to steer, Space to pause, R to reset)
void gui_snake_handle_key(u8 sc, int is_extended, int is_pressed);
// Free resources and detach PIT callback
void gui_snake_free(void);

#endif // GUI_SNAKE_H

