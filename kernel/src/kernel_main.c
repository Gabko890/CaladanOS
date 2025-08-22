#include <stdint.h>
#include <stddef.h>

#include <portio.h>
#include <vgaio.h>
#include <idt.h>


void handle_ps2() {
    uint8_t scancode = inb(0x60);  // Read keyboard data to acknowledge
    if (scancode != 0xFA) {  // Ignore ACK bytes
        vga_putchar('K');
        vga_printf(" %X \n", scancode);  // Show the scancode
    }
    outb(0x20, 0x20); // EOI to master PIC for IRQ1
}

void kernel_main(uint32_t magic, uint32_t mb2_info) {
    vga_attr(0x0B);
    vga_puts("CaladanOS");
    vga_attr(0x07);
    vga_puts(" loaded                 \n\n");

    vga_printf("kernel at: 0x%X\n", (int)&kernel_main);

    vga_printf("multiboot2 info:\n"
               "    magic: 0x%X\n"
               "    tables at: 0x%X (physical)\n\n",
               magic, mb2_info);

    extern void setup_page_tables();
    setup_page_tables();
    
    unsigned long cr3_value = 0x1000;

    __asm__ volatile (
        "mov %0, %%rax\n\t"
        "mov %%rax, %%cr3"
        :
        : "r"(cr3_value)
        : "rax"
    );
    
    vga_printf("cr3 chnged to: 0x%X\n", cr3_value);
    
    extern void irq1_handler;
    extern void default_handler;
    
    // Initialize 8259 PIC
    // ICW1: Start initialization sequence
    outb(0x20, 0x11);  // Master PIC
    outb(0xA0, 0x11);  // Slave PIC
    
    // ICW2: Set interrupt vector offsets
    outb(0x21, 0x20);  // Master PIC starts at interrupt 32
    outb(0xA1, 0x28);  // Slave PIC starts at interrupt 40
    
    // ICW3: Set up cascading
    outb(0x21, 0x04);  // Master PIC: IRQ2 connected to slave
    outb(0xA1, 0x02);  // Slave PIC: cascade identity
    
    // ICW4: Set mode
    outb(0x21, 0x01);  // 8086 mode
    outb(0xA1, 0x01);  // 8086 mode
    
    // Mask all interrupts initially
    outb(0x21, 0xFF);  // Master PIC
    outb(0xA1, 0xFF);  // Slave PIC
    
    // Set default handler for all interrupts
    for(int i = 0; i < 256; i++) {
        set_idt_entry(i, &default_handler, 0x08, 0x8e);
    }
    
    set_idt_entry(33, &irq1_handler, 0x08, 0x8e);  // IRQ1 (keyboard) = interrupt 33
    idt_load();

    vga_printf("IDT loaded\n");
    
    // Clear keyboard data
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    // Initialize PS/2 keyboard controller
    outb(0x64, 0xAE);          // Enable first PS/2 port
    
    // Wait for keyboard to be ready
    while (inb(0x64) & 0x02);
    
    outb(0x60, 0xF4);          // Enable keyboard scanning
    
    // Wait for ACK and clear it
    while (!(inb(0x64) & 0x01));
    inb(0x60);
    
    // Enable keyboard interrupt (IRQ1) in PIC
    uint8_t mask = inb(0x21);  // Read current mask
    mask &= ~0x02;             // Clear bit 1 (IRQ1)
    outb(0x21, mask);          // Write back to unmask IRQ1
    
    vga_printf("Keyboard enabled\n");

    __asm__ volatile( "sti" );
    
    while(1) __asm__ volatile( "nop" );
    
    __asm__ volatile( "hlt" );
}
