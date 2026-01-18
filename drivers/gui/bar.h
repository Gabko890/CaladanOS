#ifndef GUI_BAR_H
#define GUI_BAR_H

#include <cldtypes.h>

// Simple top bar (waybar-like) API

// Initialize (idempotent) and render the bar
void gui_bar_init(void);
void gui_bar_render(void);
// Handle a mouse click at pixel (x,y). Returns action code:
// 0 = none, 1 = open new terminal, 2 = focus window id, 3 = open new editor
int gui_bar_on_click(u32 x, u32 y, int* out_window_id);
// Handle mouse movement/hover; closes menu if cursor leaves menu/dropdown.
// Returns 1 if state changed (e.g., menu closed), else 0.
int gui_bar_on_move(u32 x, u32 y);
// Get last dropdown rectangle for background refresh after closing.
// Returns 1 if valid, else 0.
int gui_bar_get_last_dropdown_rect(u32* x, u32* y, u32* w, u32* h);
// Query current menu-open state and current dropdown rect (if open)
int gui_bar_is_menu_open(void);
int gui_bar_get_current_dropdown_rect(u32* x, u32* y, u32* w, u32* h);

// Menu management (optional)
int  gui_bar_add_menu(const char* label);   // returns menu id or -1
void gui_bar_clear_menus(void);

// Window (task list) management
int  gui_bar_register_window(const char* title); // returns window id or -1
void gui_bar_unregister_window(int id);
void gui_bar_set_active_window(int id);

// Constants
#define GUI_BAR_HEIGHT 24

#endif // GUI_BAR_H
