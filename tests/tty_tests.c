#include <cldtest.h>
#include <vgaio.h>
#include <string.h>
#include "../drivers/cldramfs/tty.h"
#include <ps2.h>

CLDTEST_SUITE(tty_tests) {}

// Test scancode to character conversion
CLDTEST_WITH_SUITE("TTY scancode conversion", tty_scancode_conversion, tty_tests) {
    // Test basic character conversion
    assert(scancode_to_char(US_A, 0) == 'a');
    assert(scancode_to_char(US_A, 1) == 'A');
    
    assert(scancode_to_char(US_1, 0) == '1');
    assert(scancode_to_char(US_1, 1) == '!');
    
    assert(scancode_to_char(US_SPACE, 0) == ' ');
    assert(scancode_to_char(US_SPACE, 1) == ' ');
    
    // Test non-printable keys
    assert(scancode_to_char(US_ENTER, 0) == 0);
    assert(scancode_to_char(US_BACKSPACE, 0) == 0);
    assert(scancode_to_char(US_ESC, 0) == 0);
}

// Test printable key detection
CLDTEST_WITH_SUITE("TTY printable keys", tty_printable_keys, tty_tests) {
    assert(is_printable_key(US_A) == 1);
    assert(is_printable_key(US_Z) == 1);
    assert(is_printable_key(US_0) == 1);
    assert(is_printable_key(US_9) == 1);
    assert(is_printable_key(US_SPACE) == 1);
    
    assert(is_printable_key(US_ENTER) == 0);
    assert(is_printable_key(US_BACKSPACE) == 0);
    assert(is_printable_key(US_ESC) == 0);
    assert(is_printable_key(US_TAB) == 0);
}

// Test special character conversion
CLDTEST_WITH_SUITE("TTY special chars", tty_special_chars, tty_tests) {
    assert(scancode_to_char(US_SEMICOLON, 0) == ';');
    assert(scancode_to_char(US_SEMICOLON, 1) == ':');
    
    assert(scancode_to_char(US_COMMA, 0) == ',');
    assert(scancode_to_char(US_COMMA, 1) == '<');
    
    assert(scancode_to_char(US_DOT, 0) == '.');
    assert(scancode_to_char(US_DOT, 1) == '>');
    
    assert(scancode_to_char(US_SLASH, 0) == '/');
    assert(scancode_to_char(US_SLASH, 1) == '?');
}

// Test bracket and quote conversion
CLDTEST_WITH_SUITE("TTY brackets quotes", tty_brackets_quotes, tty_tests) {
    assert(scancode_to_char(US_LBRACKET, 0) == '[');
    assert(scancode_to_char(US_LBRACKET, 1) == '{');
    
    assert(scancode_to_char(US_RBRACKET, 0) == ']');
    assert(scancode_to_char(US_RBRACKET, 1) == '}');
    
    assert(scancode_to_char(US_APOSTROPHE, 0) == '\'');
    assert(scancode_to_char(US_APOSTROPHE, 1) == '"');
    
    assert(scancode_to_char(US_GRAVE, 0) == '`');
    assert(scancode_to_char(US_GRAVE, 1) == '~');
}