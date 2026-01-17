#include "tty.h"
#include <vgaio.h>
#include <string.h>
#include <ps2.h>
#include "cldramfs.h"

static TTY main_tty;

void tty_init(TTY *tty) {
    tty->buffer_pos = 0;
    tty->cursor_pos = 0;
    tty->history_count = 0;
    tty->history_pos = 0;
    tty->escape_seq = 0;
    tty->escape_pos = 0;
    tty->buffer[0] = '\0';
}

void tty_reset_line(TTY *tty) {
    tty->buffer_pos = 0;
    tty->cursor_pos = 0;
    tty->buffer[0] = '\0';
}

char scancode_to_char(u8 scancode, int shift) {
    switch (scancode) {
        // Letters
        case US_A: return shift ? 'A' : 'a';
        case US_B: return shift ? 'B' : 'b';
        case US_C: return shift ? 'C' : 'c';
        case US_D: return shift ? 'D' : 'd';
        case US_E: return shift ? 'E' : 'e';
        case US_F: return shift ? 'F' : 'f';
        case US_G: return shift ? 'G' : 'g';
        case US_H: return shift ? 'H' : 'h';
        case US_I: return shift ? 'I' : 'i';
        case US_J: return shift ? 'J' : 'j';
        case US_K: return shift ? 'K' : 'k';
        case US_L: return shift ? 'L' : 'l';
        case US_M: return shift ? 'M' : 'm';
        case US_N: return shift ? 'N' : 'n';
        case US_O: return shift ? 'O' : 'o';
        case US_P: return shift ? 'P' : 'p';
        case US_Q: return shift ? 'Q' : 'q';
        case US_R: return shift ? 'R' : 'r';
        case US_S: return shift ? 'S' : 's';
        case US_T: return shift ? 'T' : 't';
        case US_U: return shift ? 'U' : 'u';
        case US_V: return shift ? 'V' : 'v';
        case US_W: return shift ? 'W' : 'w';
        case US_X: return shift ? 'X' : 'x';
        case US_Y: return shift ? 'Y' : 'y';
        case US_Z: return shift ? 'Z' : 'z';
        
        // Numbers
        case US_1: return shift ? '!' : '1';
        case US_2: return shift ? '@' : '2';
        case US_3: return shift ? '#' : '3';
        case US_4: return shift ? '$' : '4';
        case US_5: return shift ? '%' : '5';
        case US_6: return shift ? '^' : '6';
        case US_7: return shift ? '&' : '7';
        case US_8: return shift ? '*' : '8';
        case US_9: return shift ? '(' : '9';
        case US_0: return shift ? ')' : '0';
        
        // Special characters
        case US_SPACE: return ' ';
        case US_MINUS: return shift ? '_' : '-';
        case US_EQUAL: return shift ? '+' : '=';
        case US_LBRACKET: return shift ? '{' : '[';
        case US_RBRACKET: return shift ? '}' : ']';
        case US_BACKSLASH: return shift ? '|' : '\\';
        case US_SEMICOLON: return shift ? ':' : ';';
        case US_APOSTROPHE: return shift ? '"' : '\'';
        case US_GRAVE: return shift ? '~' : '`';
        case US_COMMA: return shift ? '<' : ',';
        case US_DOT: return shift ? '>' : '.';
        case US_SLASH: return shift ? '?' : '/';
        
        default: return 0;
    }
}

int is_printable_key(u8 scancode) {
    return scancode_to_char(scancode, 0) != 0;
}

int tty_handle_key(TTY *tty, u8 scancode, int is_extended) {
    u128 keyarr = ps2_keyarr();
    int shift = (keyarr & ((u128)1 << 0x2A)) || (keyarr & ((u128)1 << 0x36)); // Left or right shift
    
    if (is_extended) {
        // Handle extended keys (arrow keys, etc.)
        switch (scancode) {
            case US_ARROW_LEFT:
                if (tty->cursor_pos > 0) {
                    tty->cursor_pos--;
                    vga_printf("\x1b[D"); // Move cursor left
                }
                return 0;
            case US_ARROW_RIGHT:
                if (tty->cursor_pos < tty->buffer_pos) {
                    tty->cursor_pos++;
                    vga_printf("\x1b[C"); // Move cursor right
                }
                return 0;
            case US_ARROW_UP:
                // History navigation disabled
                return 0;
            case US_ARROW_DOWN:
                // History navigation disabled
                return 0;
        }
        return 0;
    }
    
    // Handle regular keys
    switch (scancode) {
        case US_ENTER:
            vga_putchar('\n');
            // Add to history if not empty
            if (tty->buffer_pos > 0) {
                u32 hist_idx = tty->history_count % TTY_HISTORY_SIZE;
                strcpy(tty->history[hist_idx], tty->buffer);
                if (tty->history_count < TTY_HISTORY_SIZE) {
                    tty->history_count++;
                }
            }
            return 1; // Line ready
            
        case US_BACKSPACE:
            if (tty->cursor_pos > 0) {
                // Remove character at cursor position
                for (u32 i = tty->cursor_pos - 1; i < tty->buffer_pos; i++) {
                    tty->buffer[i] = tty->buffer[i + 1];
                }
                tty->buffer_pos--;
                tty->cursor_pos--;
                tty->buffer[tty->buffer_pos] = '\0';
                
                // Update display
                vga_printf("\x1b[D"); // Move cursor left
                vga_printf("\x1b[K"); // Clear to end of line
                vga_printf("%s", &tty->buffer[tty->cursor_pos]); // Reprint remaining text
                
                // Move cursor back to correct position
                u32 chars_after = tty->buffer_pos - tty->cursor_pos;
                for (u32 i = 0; i < chars_after; i++) {
                    vga_printf("\x1b[D");
                }
            }
            return 0;
            
        case US_TAB:
            // TODO: Auto-completion
            return 0;
            
        case US_ESC:
            // Clear current line
            tty_reset_line(tty);
            vga_printf("\x1b[2K\x1b[G"); // Clear line and move to beginning
            tty_print_prompt();
            return 0;
            
        default:
            if (is_printable_key(scancode) && tty->buffer_pos < TTY_BUFFER_SIZE - 1) {
                char c = scancode_to_char(scancode, shift);
                if (c) {
                    // Insert character at cursor position
                    for (u32 i = tty->buffer_pos; i > tty->cursor_pos; i--) {
                        tty->buffer[i] = tty->buffer[i - 1];
                    }
                    tty->buffer[tty->cursor_pos] = c;
                    tty->buffer_pos++;
                    tty->cursor_pos++;
                    tty->buffer[tty->buffer_pos] = '\0';
                    
                    // Update display - echo character to screen
                    vga_putchar(c);
                    
                    // If we inserted in the middle, reprint the rest of the line
                    if (tty->cursor_pos < tty->buffer_pos) {
                        // Reprint characters after cursor
                        vga_printf("%s", &tty->buffer[tty->cursor_pos]);
                        // Move cursor back to correct position
                        u32 chars_after = tty->buffer_pos - tty->cursor_pos;
                        for (u32 i = 0; i < chars_after; i++) {
                            vga_printf("\x1b[D");
                        }
                    }
                }
            }
            return 0;
    }
}

int tty_is_line_ready(TTY *tty) {
    return tty->buffer_pos > 0 && tty->buffer[tty->buffer_pos - 1] == '\0';
}

char* tty_get_line(TTY *tty) {
    return tty->buffer;
}

void tty_print_prompt(void) {
    char path[256];
    
    // Safety check
    if (!ramfs_cwd || !ramfs_root) {
        vga_printf("[ ? ] ");
        vga_attr(0x05); // Purple for $
        vga_printf("$ ");
        vga_attr(0x07); // Reset to white
        return;
    }
    
    // If at root, simple case
    if (ramfs_cwd == ramfs_root) {
        vga_printf("[ / ] ");
    } else {
        // Build path carefully
        Node *nodes[10];  // Limit depth for safety
        int depth = 0;
        Node *current = ramfs_cwd;
        
        // Collect path components
        while (current && current != ramfs_root && depth < 10) {
            if (current->name) {
                nodes[depth] = current;
                depth++;
            }
            current = current->parent;
        }
        
        // Build the path string
        path[0] = '\0';
        strcat(path, "/");
        
        for (int i = depth - 1; i >= 0; i--) {
            if (nodes[i] && nodes[i]->name) {
                strcat(path, nodes[i]->name);
                if (i > 0) strcat(path, "/");
            }
        }
        
        vga_printf("[ %s ] ", path);
    }
    
    vga_attr(0x05); // Purple for $
    vga_printf("$ ");
    vga_attr(0x07); // Reset to white
}

// Global TTY interface functions
void tty_global_init(void) {
    tty_init(&main_tty);
}

int tty_global_handle_key(u8 scancode, int is_extended) {
    return tty_handle_key(&main_tty, scancode, is_extended);
}

char* tty_global_get_line(void) {
    return tty_get_line(&main_tty);
}

void tty_global_reset_line(void) {
    tty_reset_line(&main_tty);
}
