#ifndef GUI_GUI_H
#define GUI_GUI_H

#include <cldtypes.h>

// Start a very simple GUI: draws a window and a mouse cursor.
// Requires a framebuffer to be present. Safe to call multiple times.
void gui_start(void);

// Optional helpers to open specific windows programmatically
void gui_open_snake(void);

#endif // GUI_GUI_H
