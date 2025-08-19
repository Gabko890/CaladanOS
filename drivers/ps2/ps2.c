#include <ps2.h>
#include <portio.h>
#include <stdint.h>
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

void ps2_handler(void) {
    uint8_t scancode = inb(0x60);  // Read keyboard data to acknowledge
    if (scancode != 0xFA) {  // Ignore ACK bytes
        vga_putchar('K');
        vga_printf(" %X \n", scancode);  // Show the scancode
    }
}
