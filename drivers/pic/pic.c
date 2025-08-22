#include <pic.h>
#include <portio.h>

void pic_init(void) {
    // ICW1: Start initialization sequence
    outb(PIC_MASTER_CMD, 0x11);  // Master PIC
    outb(PIC_SLAVE_CMD, 0x11);   // Slave PIC
    
    // ICW2: Set interrupt vector offsets
    outb(PIC_MASTER_DATA, 0x20); // Master PIC starts at interrupt 32
    outb(PIC_SLAVE_DATA, 0x28);  // Slave PIC starts at interrupt 40
    
    // ICW3: Set up cascading
    outb(PIC_MASTER_DATA, 0x04); // Master PIC: IRQ2 connected to slave
    outb(PIC_SLAVE_DATA, 0x02);  // Slave PIC: cascade identity
    
    // ICW4: Set mode
    outb(PIC_MASTER_DATA, 0x01); // 8086 mode
    outb(PIC_SLAVE_DATA, 0x01);  // 8086 mode
    
    // Mask all interrupts initially
    outb(PIC_MASTER_DATA, 0xFF); // Master PIC
    outb(PIC_SLAVE_DATA, 0xFF);  // Slave PIC
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC_SLAVE_CMD, PIC_EOI);
    }
    outb(PIC_MASTER_CMD, PIC_EOI);
}

void pic_enable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_disable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}