#ifndef GUI_WALLPAPER_H
#define GUI_WALLPAPER_H

#include <cldtypes.h>

// Load and prepare wallpaper from RAMFS BMP path (e.g., "/wallpapers/xyz.bmp").
// Returns 1 on success, 0 on failure.
int gui_wallpaper_load(const char* path);

// Is wallpaper loaded and prepared for drawing?
int gui_wallpaper_is_loaded(void);

// Draw the wallpaper to cover the full screen.
void gui_wallpaper_draw_fullscreen(void);

// Redraw a rectangular screen region using the wallpaper (no-op if not loaded).
void gui_wallpaper_redraw_rect(u32 x, u32 y, u32 w, u32 h);

#endif // GUI_WALLPAPER_H

