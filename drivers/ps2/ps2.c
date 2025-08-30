#include <ps2.h>
#include <portio.h>
#include <cldtypes.h>
#include <vgaio.h>

void ps2_init(void) {
    // Clear any pending keyboard data
    while (inb(0x64) & 0x01) {
        inb(0x60);  // Read and discard
    }
    
    // Initialize PS/2 keyboard controller
    outb(0x64, 0xAE);          // Enable first PS/2 port
    
    // Wait for keyboard to be ready
    while (inb(0x64) & 0x02);
    
    outb(0x60, 0xF4);          // Enable keyboard scanning
    
    // Wait for ACK and clear it
    while (!(inb(0x64) & 0x01));
    inb(0x60);  // Read ACK byte
}

static u128 keyarr = 0;
static int expecting_extended = 0;
static ps2_key_callback_t key_callback = NULL;

void ps2_set_key_callback(ps2_key_callback_t callback) {
    key_callback = callback;
}

void ps2_handler(void) {
    u8 scancode = inb(0x60);  // Read keyboard data to acknowledge
    
    if (scancode == 0xFA) {  // Ignore ACK bytes
        return;
    }
    
    if (scancode == 0xE0) {  // Extended key prefix
        expecting_extended = 1;
        return;
    }
    
    int is_extended = expecting_extended;
    expecting_extended = 0;
    
    int is_pressed = (scancode < 0x80);
    u8 base_scancode = is_pressed ? scancode : (scancode - 0x80);
    
    // Update keyarr for non-extended keys only
    if (!is_extended) {
        if (is_pressed) {
            keyarr |= (u128)1 << base_scancode;
        } else {
            keyarr &= ~((u128)1 << base_scancode);
        }
    }
    
    // Call callback if registered and key is pressed
    if (key_callback && is_pressed) {
        key_callback(base_scancode, is_extended, is_pressed);
    }
}

u128 ps2_keyarr() {
    return keyarr;
}
