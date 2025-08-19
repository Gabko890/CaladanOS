#ifndef PS2_H
#define PS2_H

#include <stdbool.h>
#include <stdint.h>

void ps2_init(void);
void ps2_handler(void);

#define US_A 0x1E
#define US_B 0x30
#define US_C 0x2E
#define US_D 0x20
#define US_E 0x12
#define US_F 0x21
#define US_G 0x22
#define US_H 0x23
#define US_I 0x17
#define US_J 0x24
#define US_K 0x25
#define US_L 0x26
#define US_M 0x32
#define US_N 0x31
#define US_O 0x18
#define US_P 0x19
#define US_Q 0x10
#define US_R 0x13
#define US_S 0x1F
#define US_T 0x14
#define US_U 0x16
#define US_V 0x2F
#define US_W 0x11
#define US_X 0x2D
#define US_Y 0x15
#define US_Z 0x2C

// Numbers (top row)
#define US_1 0x02
#define US_2 0x03
#define US_3 0x04
#define US_4 0x05
#define US_5 0x06
#define US_6 0x07
#define US_7 0x08
#define US_8 0x09
#define US_9 0x0A
#define US_0 0x0B

// Special keys
#define US_ENTER       0x1C
#define US_ESC         0x01
#define US_BACKSPACE   0x0E
#define US_TAB         0x0F
#define US_SPACE       0x39
#define US_MINUS       0x0C  // -
#define US_EQUAL       0x0D  // =
#define US_LBRACKET    0x1A  // [
#define US_RBRACKET    0x1B  // ]
#define US_BACKSLASH   0x2B  // \
#define US_SEMICOLON   0x27  // ;
#define US_APOSTROPHE  0x28  // '
#define US_GRAVE       0x29  // `
#define US_COMMA       0x33  // ,
#define US_DOT         0x34  // .
#define US_SLASH       0x35  // /


#endif //  PS2_H
