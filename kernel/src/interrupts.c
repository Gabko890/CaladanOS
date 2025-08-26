#include <interrupts/interrupts.h>
#include <idt.h>
#include <pic.h>

static interrupt_handler_t interrupt_handlers[256];

void interrupts_init(void) {
    // Initialize PIC
    pic_init();
    
    // Set all handlers to default initially
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = default_interrupt_handler;
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    // Load IDT
    idt_load();
}

void register_interrupt_handler(u8 interrupt_num, interrupt_handler_t handler) {
    interrupt_handlers[interrupt_num] = handler;
    set_idt_entry(interrupt_num, handler, 0x08, 0x8e);
}

void interrupts_enable(void) {
    __asm__ volatile("sti");
}

void interrupts_disable(void) {
    __asm__ volatile("cli");
}

void default_interrupt_handler(void) {
    // Send EOI to PIC for any unhandled interrupt
    pic_send_eoi(0);  // Will handle both master and slave
}