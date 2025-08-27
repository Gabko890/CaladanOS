#ifndef PS2_H
#define PS2_H

#include <stdbool.h>
#include <cldtypes.h>

void ps2_init(void);
void ps2_handler(void);
u128 ps2_keyarr(void);

// Letters
#define US_A        0x1E
#define US_A_REL    0x9E
#define US_B        0x30
#define US_B_REL    0xB0
#define US_C        0x2E
#define US_C_REL    0xAE
#define US_D        0x20
#define US_D_REL    0xA0
#define US_E        0x12
#define US_E_REL    0x92
#define US_F        0x21
#define US_F_REL    0xA1
#define US_G        0x22
#define US_G_REL    0xA2
#define US_H        0x23
#define US_H_REL    0xA3
#define US_I        0x17
#define US_I_REL    0x97
#define US_J        0x24
#define US_J_REL    0xA4
#define US_K        0x25
#define US_K_REL    0xA5
#define US_L        0x26
#define US_L_REL    0xA6
#define US_M        0x32
#define US_M_REL    0xB2
#define US_N        0x31
#define US_N_REL    0xB1
#define US_O        0x18
#define US_O_REL    0x98
#define US_P        0x19
#define US_P_REL    0x99
#define US_Q        0x10
#define US_Q_REL    0x90
#define US_R        0x13
#define US_R_REL    0x93
#define US_S        0x1F
#define US_S_REL    0x9F
#define US_T        0x14
#define US_T_REL    0x94
#define US_U        0x16
#define US_U_REL    0x96
#define US_V        0x2F
#define US_V_REL    0xAF
#define US_W        0x11
#define US_W_REL    0x91
#define US_X        0x2D
#define US_X_REL    0xAD
#define US_Y        0x15
#define US_Y_REL    0x95
#define US_Z        0x2C
#define US_Z_REL    0xAC

// Numbers
#define US_1        0x02
#define US_1_REL    0x82
#define US_2        0x03
#define US_2_REL    0x83
#define US_3        0x04
#define US_3_REL    0x84
#define US_4        0x05
#define US_4_REL    0x85
#define US_5        0x06
#define US_5_REL    0x86
#define US_6        0x07
#define US_6_REL    0x87
#define US_7        0x08
#define US_7_REL    0x88
#define US_8        0x09
#define US_8_REL    0x89
#define US_9        0x0A
#define US_9_REL    0x8A
#define US_0        0x0B
#define US_0_REL    0x8B

// Special keys
#define US_ENTER        0x1C
#define US_ENTER_REL    0x9C
#define US_ESC          0x01
#define US_ESC_REL      0x81
#define US_BACKSPACE    0x0E
#define US_BACKSPACE_REL 0x8E
#define US_TAB          0x0F
#define US_TAB_REL      0x8F
#define US_SPACE        0x39
#define US_SPACE_REL    0xB9
#define US_MINUS        0x0C
#define US_MINUS_REL    0x8C
#define US_EQUAL        0x0D
#define US_EQUAL_REL    0x8D
#define US_LBRACKET     0x1A
#define US_LBRACKET_REL 0x9A
#define US_RBRACKET     0x1B
#define US_RBRACKET_REL 0x9B
#define US_BACKSLASH    0x2B
#define US_BACKSLASH_REL 0xAB
#define US_SEMICOLON    0x27
#define US_SEMICOLON_REL 0xA7
#define US_APOSTROPHE   0x28
#define US_APOSTROPHE_REL 0xA8
#define US_GRAVE        0x29
#define US_GRAVE_REL    0xA9
#define US_COMMA        0x33
#define US_COMMA_REL    0xB3
#define US_DOT          0x34
#define US_DOT_REL      0xB4
#define US_SLASH        0x35
#define US_SLASH_REL    0xB5

// Arrow keys (extended: 0xE0 prefix)
#define US_ARROW_UP         0x48
#define US_ARROW_UP_REL     0xC8
#define US_ARROW_DOWN       0x50
#define US_ARROW_DOWN_REL   0xD0
#define US_ARROW_LEFT       0x4B
#define US_ARROW_LEFT_REL   0xCB
#define US_ARROW_RIGHT      0x4D
#define US_ARROW_RIGHT_REL  0xCD

#endif // PS2_H
