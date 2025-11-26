#include <vgaio.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <portio.h>
#include <fb/fb_console.h>


static volatile char* vga_addr = (volatile char*) 0xb8000;
static Cursor cursor = {0, 0};
static u8 arrt = 0x07;

// ANSI escape sequence parsing state
typedef enum {
    ANSI_STATE_NORMAL,
    ANSI_STATE_ESCAPE,
    ANSI_STATE_CSI
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_STATE_NORMAL;
static char ansi_buffer[16];
static int ansi_buffer_pos = 0;


void vga_update_cursor(int x, int y) {
    if (fb_console_present()) return; // no hardware cursor in FB mode
	u16 pos = y * VGA_WIDTH + x;
	outb(0x3D4, 0x0F);
	outb(0x3D5, (u8) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (u8) ((pos >> 8) & 0xFF));
}

//  ===================== output =========================
//
static inline int text_width(void)  { if (fb_console_is_active()) { int w,h; fb_console_get_size(&w,&h); return w; } return VGA_WIDTH; }
static inline int text_height(void) { if (fb_console_is_active()) { int w,h; fb_console_get_size(&w,&h); return h; } return VGA_HEIGHT; }

static void vga_scroll_up(void) {
    if (fb_console_present()) {
        fb_console_scroll_up(arrt);
        return;
    }
    for (int row = 0; row < VGA_HEIGHT - 1; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            int current_pos = (row * VGA_WIDTH + col) * 2;
            int next_pos = ((row + 1) * VGA_WIDTH + col) * 2;
            vga_addr[current_pos] = vga_addr[next_pos];
            vga_addr[current_pos + 1] = vga_addr[next_pos + 1];
        }
    }
    // Clear the last line
    int last_row_start = (VGA_HEIGHT - 1) * VGA_WIDTH * 2;
    for (int i = 0; i < VGA_WIDTH * 2; i += 2) {
        vga_addr[last_row_start + i] = ' ';
        vga_addr[last_row_start + i + 1] = arrt;
    }
}

static void vga_handle_ansi_sequence(void) {
    ansi_buffer[ansi_buffer_pos] = '\0';
    
    // Parse common ANSI escape sequences
    if (ansi_buffer_pos == 1) {
        switch (ansi_buffer[0]) {
            case 'C': // Move cursor right
                if (cursor.x < VGA_WIDTH - 1) {
                    cursor.x++;
                    vga_update_cursor(cursor.x, cursor.y);
                }
                break;
            case 'D': // Move cursor left
                if (cursor.x > 0) {
                    cursor.x--;
                    vga_update_cursor(cursor.x, cursor.y);
                }
                break;
            case 'H': // Move cursor to home position
                cursor.x = 0;
                cursor.y = 0;
                vga_update_cursor(cursor.x, cursor.y);
                break;
            case 'K': // Clear to end of line
                vga_clear_to_eol();
                break;
            case 'G': // Move cursor to beginning of line
                cursor.x = 0;
                vga_update_cursor(cursor.x, cursor.y);
                break;
        }
    } else if (ansi_buffer_pos == 2) {
        if (ansi_buffer[0] == '2') {
            switch (ansi_buffer[1]) {
                case 'J': // Clear entire screen
                    vga_clear_screen();
                    cursor.x = 0;
                    cursor.y = 0;
                    vga_update_cursor(cursor.x, cursor.y);
                    break;
                case 'K': // Clear entire line
                    vga_clear_line();
                    break;
            }
        }
    }
    
    // Reset state
    ansi_state = ANSI_STATE_NORMAL;
    ansi_buffer_pos = 0;
}

void vga_putchar(char c) {
    // Handle ANSI escape sequences
    switch (ansi_state) {
        case ANSI_STATE_NORMAL:
            if (c == '\x1b') {
                ansi_state = ANSI_STATE_ESCAPE;
                ansi_buffer_pos = 0;
                return;
            }
            break;
            
        case ANSI_STATE_ESCAPE:
            if (c == '[') {
                ansi_state = ANSI_STATE_CSI;
                ansi_buffer_pos = 0;
                return;
            } else {
                // Not a CSI sequence, reset to normal
                ansi_state = ANSI_STATE_NORMAL;
                ansi_buffer_pos = 0;
            }
            break;
            
        case ANSI_STATE_CSI:
            if ((c >= '0' && c <= '9') || c == ';') {
                // Parameter character, store it
                if (ansi_buffer_pos < (int)(sizeof(ansi_buffer) - 1)) {
                    ansi_buffer[ansi_buffer_pos++] = c;
                }
                return;
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                // Final character, store and process
                if (ansi_buffer_pos < (int)(sizeof(ansi_buffer) - 1)) {
                    ansi_buffer[ansi_buffer_pos++] = c;
                }
                vga_handle_ansi_sequence();
                return;
            } else {
                // Invalid sequence, reset to normal
                ansi_state = ANSI_STATE_NORMAL;
                ansi_buffer_pos = 0;
            }
            break;
    }
    
    // Normal character processing
    if (c == '\n') {
        cursor.x = 0;
        cursor.y++;
    } else {
        // Bounds check before calculating position
        int w = text_width();
        int h = text_height();
        if (cursor.y >= h) {
            cursor.y = h - 1;
            vga_scroll_up();
        }
        if (cursor.x >= w) {
            cursor.x = 0;
            cursor.y++;
            if (cursor.y >= h) {
                cursor.y = h - 1;
                vga_scroll_up();
            }
        }

        if (fb_console_present()) {
            fb_console_putc_at(c, arrt, cursor.x, cursor.y);
        } else {
            int relative_pos = (cursor.y * VGA_WIDTH + cursor.x) * 2;
            vga_addr[relative_pos] = c;
            vga_addr[relative_pos + 1] = arrt;
        }
        cursor.x++;

        if (cursor.x >= w) {
            cursor.x = 0;
            cursor.y++;
        }
    }

    // Handle cursor.y overflow after any operation
    int h2 = text_height();
    if (cursor.y >= h2) {
        cursor.y = h2 - 1;
        vga_scroll_up();
    }

    vga_update_cursor(cursor.x, cursor.y);

    #ifdef QEMU_ISA_DEBUGCON
    outb(0xe9, c);
    #endif
}

void vga_attr(u8 _arrt) {
    arrt = _arrt;
}

void vga_clear_screen(void) {
    if (fb_console_present()) {
        fb_console_set_color(arrt);
        fb_console_clear();
    } else {
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH * 2; i += 2) {
            vga_addr[i] = ' ';
            vga_addr[i + 1] = arrt;
        }
    }
    cursor.x = 0;
    cursor.y = 0;
    vga_update_cursor(cursor.x, cursor.y);
}

void vga_clear_line(void) {
    if (fb_console_present()) {
        fb_console_clear_line(cursor.y, arrt);
    } else {
        int line_start = cursor.y * VGA_WIDTH * 2;
        for (int i = 0; i < VGA_WIDTH * 2; i += 2) {
            vga_addr[line_start + i] = ' ';
            vga_addr[line_start + i + 1] = arrt;
        }
    }
}

void vga_clear_to_eol(void) {
    if (fb_console_present()) {
        fb_console_clear_to_eol(cursor.x, cursor.y, arrt);
    } else {
        int pos = (cursor.y * VGA_WIDTH + cursor.x) * 2;
        int line_end = (cursor.y * VGA_WIDTH + VGA_WIDTH) * 2;
        for (int i = pos; i < line_end; i += 2) {
            vga_addr[i] = ' ';
            vga_addr[i + 1] = arrt;
        }
    }
}

void vga_move_cursor_left(void) {
    if (cursor.x > 0) {
        cursor.x--;
        vga_update_cursor(cursor.x, cursor.y);
    }
}

void vga_move_cursor_right(void) {
    int w = text_width();
    if (cursor.x < w - 1) {
        cursor.x++;
        vga_update_cursor(cursor.x, cursor.y);
    }
}

void vga_move_cursor_home(void) {
    cursor.x = 0;
    cursor.y = 0;
    vga_update_cursor(cursor.x, cursor.y);
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
