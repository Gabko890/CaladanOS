#include <ps2.h>
#include <portio.h>
#include <cldtypes.h>
#include <vgaio.h>

// ====== Low-level controller helpers ======
static void ps2_wait_input_clear(void) {
    // Wait until input buffer is clear (bit 1 == 0)
    while (inb(0x64) & 0x02) { }
}

static void ps2_wait_output_full(void) {
    // Wait until output buffer is full (bit 0 == 1)
    while (!(inb(0x64) & 0x01)) { }
}

static void ps2_write(u8 data) {
    ps2_wait_input_clear();
    outb(0x60, data);
}

static void ps2_write_cmd(u8 cmd) {
    ps2_wait_input_clear();
    outb(0x64, cmd);
}

static u8 ps2_read_data(void) {
    ps2_wait_output_full();
    return inb(0x60);
}

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

// ===== PS/2 mouse (aux) =====
static ps2_mouse_callback_t mouse_callback = 0;
static u8 mouse_packet[3];
static int mouse_packet_index = 0;

static void mouse_write(u8 data) {
    // Send a byte to the mouse via 0xD4 prefix
    ps2_write_cmd(0xD4);
    ps2_write(data);
    // Read ACK (0xFA)
    (void)ps2_read_data();
}

void ps2_mouse_set_callback(ps2_mouse_callback_t callback) {
    mouse_callback = callback;
}

void ps2_mouse_init(void) {
    // Enable auxiliary device (mouse)
    ps2_write_cmd(0xA8);

    // Enable IRQ12 in controller command byte
    ps2_write_cmd(0x20); // Read command byte
    u8 status = ps2_read_data();
    status |= 0x02;      // Enable IRQ12 (bit 1)
    ps2_write_cmd(0x60); // Write command byte
    ps2_write(status);

    // Reset defaults and enable data reporting
    mouse_write(0xF6);   // Set defaults
    mouse_write(0xF4);   // Enable data reporting

    mouse_packet_index = 0;
}

void ps2_mouse_handler(void) {
    // Read one byte; since this runs on IRQ12, data belongs to mouse
    u8 data = inb(0x60);

    // Synchronize on first byte with bit 3 set
    if (mouse_packet_index == 0 && !(data & 0x08)) {
        return; // discard until in sync
    }

    mouse_packet[mouse_packet_index++] = data;
    if (mouse_packet_index < 3) return;

    mouse_packet_index = 0;

    u8 b = mouse_packet[0];
    int dx = (int)((i8)mouse_packet[1]);
    int dy = (int)((i8)mouse_packet[2]);
    // In PS/2, positive dy = up (or down?) — invert to screen coords (y increases downward)
    dy = -dy;

    if (mouse_callback) {
        mouse_callback(dx, dy, (u8)(b & 0x07));
    }
}
