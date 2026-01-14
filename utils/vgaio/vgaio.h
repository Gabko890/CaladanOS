#ifndef VGAIO_H
#define VGAIO_H

#include <cldtypes.h>
#include <stdarg.h>

#define VGA_HEIGHT 25
#define VGA_WIDTH  80

typedef struct {
    u8 x;
    u8 y;
} Cursor;

void vga_putchar(char);
void vga_attr(u8);
void vga_update_cursor(int x, int y);
void vga_clear_screen(void);
void vga_clear_line(void);
void vga_clear_to_eol(void);
void vga_move_cursor_left(void);
void vga_move_cursor_right(void);
void vga_move_cursor_home(void);

int vga_printf(const char*, ...);

// Optional output redirection for GUI terminal
typedef void (*vga_putchar_sink_t)(char);
typedef void (*vga_attr_sink_t)(u8);
void vga_set_putchar_sink(vga_putchar_sink_t fn, int suppress_default);
void vga_clear_putchar_sink(void);
void vga_set_attr_sink(vga_attr_sink_t fn, int suppress_default);
void vga_clear_attr_sink(void);

#endif //  vgaio.h
