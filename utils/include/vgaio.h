#ifndef VGAIO_H
#define VGAIO_H

#include <stdint.h>
#include <stdarg.h>

typedef struct {
  uint8_t x;
  uint8_t y;
} Cursor;

void vga_putchar(char);
int  vga_puts(const char*);
void vga_attr(uint8_t);

int vga_printf(const char*, ...);

#endif //  vgaio.h
