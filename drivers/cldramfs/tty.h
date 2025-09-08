#ifndef TTY_H
#define TTY_H

#include <cldtypes.h>

#define TTY_BUFFER_SIZE 256
#define TTY_HISTORY_SIZE 10

struct tty {
    char buffer[TTY_BUFFER_SIZE];
    u32 buffer_pos;
    u32 cursor_pos;
    char history[TTY_HISTORY_SIZE][TTY_BUFFER_SIZE];
    u32 history_count;
    u32 history_pos;
    int escape_seq;
    u8 escape_buffer[8];
    u32 escape_pos;
};

// TTY functions
void tty_init(struct tty *tty);
void tty_reset_line(struct tty *tty);
int tty_handle_key(struct tty *tty, u8 scancode, int is_extended);
int tty_is_line_ready(struct tty *tty);
char *tty_get_line(struct tty *tty);
void tty_print_prompt(void);

// Global TTY functions for kernel integration
void tty_global_init(void);
int tty_global_handle_key(u8 scancode, int is_extended);
char *tty_global_get_line(void);
void tty_global_reset_line(void);

// Key handling functions
char scancode_to_char(u8 scancode, int shift);
int is_printable_key(u8 scancode);

#endif // TTY_H