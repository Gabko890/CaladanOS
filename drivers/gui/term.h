#ifndef GUI_TERM_H
#define GUI_TERM_H

#include <cldtypes.h>

// Initialize terminal region inside given pixel rect
void gui_term_init(u32 px, u32 py, u32 pw, u32 ph);
// Attach sinks so all vga_printf output goes to the terminal
void gui_term_attach(void);
// Detach sinks and flush (no-op)
void gui_term_detach(void);
// Optional: set current attribute (from vga_attr)
void gui_term_set_attr(u8 attr);
// Handle a single output character (sink entrypoint)
void gui_term_putchar(char c);
// Move terminal viewport to new pixel position (keeps content)
void gui_term_move(u32 px, u32 py);
// Redraw full terminal content from backing store
void gui_term_render_all(void);
// Free terminal backing store
void gui_term_free(void);

#endif // GUI_TERM_H
