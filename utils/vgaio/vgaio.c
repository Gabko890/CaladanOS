#include <vgaio.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <portio.h>


static volatile char* vga_addr = (volatile char*) 0xb8000;
static Cursor cursor = {0, 0};
static u8 arrt = 0x07;


static void vga_update_cursor(int x, int y) {
	u16 pos = y * VGA_WIDTH + x;

	outb(0x3D4, 0x0F);
	outb(0x3D5, (u8) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (u8) ((pos >> 8) & 0xFF));
}

//  ===================== output =========================
//
void vga_putchar(char c) {
    if (c == '\n') {
        cursor.x = 0;
        cursor.y++;
    } else {
        int relative_pos = (cursor.y * VGA_WIDTH + cursor.x) * 2;
        vga_addr[relative_pos] = c;
        vga_addr[relative_pos + 1] = arrt;
        cursor.x++;

        if (cursor.x >= VGA_WIDTH) { // FIX: >= instead of >
            cursor.x = 0;
            cursor.y++;
        }
    }

    vga_update_cursor(cursor.x, cursor.y);

    #ifdef QEMU_ISA_DEBUGCON
    outb(0xe9, c);
    #endif
}

int vga_puts(const char *string) {
    if (!string)
        return 1;

    volatile char *video = (volatile char*)vga_addr;

    while( *string != 0 ) {
        if (*string != '\n') {
            int relative_pos = (cursor.y * VGA_WIDTH + cursor.x) * 2;
            video[relative_pos] = *string;
            video[relative_pos + 1] = arrt;
            cursor.x++;
        }

        if (cursor.x > 80 || *string == '\n') {
            cursor.x = 0;
            cursor.y++;
        }
        
        #ifdef QEMU_ISA_DEBUGCON
        outb(0xe9, *string);
        #endif
        
        *string++;
    }

    vga_update_cursor(cursor.x, cursor.y); 
    
    return 0;
}

void vga_attr(u8 _arrt) {
    arrt = _arrt;
}

static void vga_put_uint(unsigned int val, unsigned int base, bool upper) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (val == 0) {
        vga_putchar('0');
        return;
    }
    while (val > 0) {
        buf[i++] = digits[val % base];
        val /= base;
    }
    while (i--) vga_putchar(buf[i]);
}

static void vga_put_ulong(unsigned long val, unsigned int base, bool upper) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (val == 0) {
        vga_putchar('0');
        return;
    }
    while (val > 0) {
        buf[i++] = digits[val % base];
        val /= base;
    }
    while (i--) vga_putchar(buf[i]);
}

static void vga_put_int(int val) {
    if (val < 0) {
        vga_putchar('-');
        vga_put_uint((unsigned int)(-val), 10, false);
    } else {
        vga_put_uint((unsigned int)val, 10, false);
    }
}

static void vga_put_ulonglong(unsigned long long val, unsigned int base, bool upper) {
    char buf[64];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (val == 0) {
        vga_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = digits[val % base];
        val /= base;
    }
    while (i--) vga_putchar(buf[i]);
}

static void vga_put_longlong(long long val) {
    if (val < 0) {
        vga_putchar('-');
        vga_put_ulonglong((unsigned long long)(-val), 10, false);
    } else {
        vga_put_ulonglong((unsigned long long)val, 10, false);
    }
}

static void vga_put_long(long val) {
    if (val < 0) {
        vga_putchar('-');
        vga_put_ulong((unsigned long)(-val), 10, false);
    } else {
        vga_put_ulong((unsigned long)val, 10, false);
    }
}

int vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int count = 0;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    vga_putchar(c);
                    count++;
                } break;

                case 's': {
                    const char *s = va_arg(args, const char*);
                    while (*s) { vga_putchar(*s++); count++; }
                } break;

                case 'd':
                case 'i': {
                    int v = va_arg(args, int);
                    vga_put_int(v);
                } break;

                case 'u': {
                    unsigned int v = va_arg(args, unsigned int);
                    vga_put_uint(v, 10, false);
                } break;

                case 'x': {
                    unsigned int v = va_arg(args, unsigned int);
                    vga_put_uint(v, 16, false);
                } break;

                case 'X': {
                    unsigned int v = va_arg(args, unsigned int);
                    vga_put_uint(v, 16, true);
                } break;

                case 'l': {
                    fmt++;
                    if (*fmt == 'l') { 
                        fmt++;
                        if (*fmt == 'd' || *fmt == 'i') {
                            long long v = va_arg(args, long long);
                            vga_put_longlong(v);
                        } else if (*fmt == 'u') {
                            unsigned long long v = va_arg(args, unsigned long long);
                            vga_put_ulonglong(v, 10, false);
                        } else if (*fmt == 'x') {
                            unsigned long long v = va_arg(args, unsigned long long);
                            vga_put_ulonglong(v, 16, false);
                        } else if (*fmt == 'X') {
                            unsigned long long v = va_arg(args, unsigned long long);
                            vga_put_ulonglong(v, 16, true);
                        }
                    } else { 
                        if (*fmt == 'd' || *fmt == 'i') {
                            long v = va_arg(args, long);
                            vga_put_long(v);
                        } else if (*fmt == 'u') {
                            unsigned long v = va_arg(args, unsigned long);
                            vga_put_ulong(v, 10, false);
                        } else if (*fmt == 'x') {
                            unsigned long v = va_arg(args, unsigned long);
                            vga_put_ulong(v, 16, false);
                        } else if (*fmt == 'X') {
                            unsigned long v = va_arg(args, unsigned long);
                            vga_put_ulong(v, 16, true);
                        }
                    }
                } break;

                case 'z': {
                    fmt++;
                    if (*fmt == 'u') {
                        size_t v = va_arg(args, size_t);
                        vga_put_ulonglong(v, 10, false);
                    } else if (*fmt == 'x') {
                        size_t v = va_arg(args, size_t);
                        vga_put_ulonglong(v, 16, false);
                    } else if (*fmt == 'X') {
                        size_t v = va_arg(args, size_t);
                        vga_put_ulonglong(v, 16, true);
                    }
                } break;

                case '%': {
                    vga_putchar('%');
                    count++;
                } break;

                default:
                    vga_putchar('%');
                    vga_putchar(*fmt);
                    count += 2;
                    break;
            }
        } else {
            if (*fmt == '\n') {
                vga_putchar('\n');
                count++;
            } else if (*fmt == '\t') {
                vga_putchar(' '); vga_putchar(' ');
                vga_putchar(' '); vga_putchar(' ');
                count += 4;
            } else {
                vga_putchar(*fmt);
                count++;
            }
        }
        fmt++;
    }

    va_end(args);
    return count;
}

// ====================== input =======================
