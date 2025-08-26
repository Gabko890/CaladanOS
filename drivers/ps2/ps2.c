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

void ps2_handler(void) {
    u8 scancode = inb(0x60);  // Read keyboard data to acknowledge
    if (scancode != 0xFA) {  // Ignore ACK bytes
        if (scancode < 0x80) {
            keyarr |= (u128)1 << scancode;
        } else {
            keyarr &= ~((u128)1 << (scancode - 0x80));
        }

        if (keyarr & ((u128)1 << US_A)) vga_putchar('a');
        //else if (keyarr << US_B) vga_putchar('b');
    }
}

u128 ps2_keyarr() {
    return keyarr;
}
