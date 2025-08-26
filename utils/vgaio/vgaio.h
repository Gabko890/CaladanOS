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
int  vga_puts(const char*);
void vga_attr(u8);

int vga_printf(const char*, ...);

#endif //  vgaio.h
