#ifndef GUI_GUI_H
#define GUI_GUI_H

#include <cldtypes.h>

// Start a very simple GUI: draws a window and a mouse cursor.
// Requires a framebuffer to be present. Safe to call multiple times.
void gui_start(void);
int gui_reload_config(void);
int gui_reload_wallpaper(void);
int gui_change_wallpaper(const char *path);

// Optional helpers to open specific windows programmatically
void gui_open_snake(void);
void gui_close_terminal(void);
void gui_restore_input(void);

#endif // GUI_GUI_H
