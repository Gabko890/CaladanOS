#include <stdint.h>
#include <stddef.h>

#include <portio.h>
#include <vgaio.h>
#include <interrupts/interrupts.h>
#include <ps2.h>
#include <pic.h>
#include <multiboot/multiboot2.h>

void handle_ps2() {
    ps2_handler();
    pic_send_eoi(1); // Send EOI for IRQ1
}

void kernel_main(uint32_t magic, uint32_t mb2_info) {
    vga_attr(0x0B);
    vga_puts("CaladanOS");
    vga_attr(0x07);
    vga_puts(" loaded        \n\n");

    multiboot2_parse(magic, mb2_info);
    
    volatile char* ramfs = (volatile char*)0x10b000; // temporary hardcoded famfs start
    volatile char* fs_file = ramfs + 0x110;          // just jump over cpio header (newc)

    for (int i = 0; i < 512/*size of cpio module hardcoded*/; i++) {
        vga_putchar(ramfs[i]); // print whole medule
        outb(0xe9, ramfs[i]);  // qemu serial
    }

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
    
    vga_printf("\ncr3 chnged to: 0x%X\n", cr3_value);
    
    extern void irq1_handler();
    
    // interrupt system (PIC + IDT)
    interrupts_init();
    vga_printf("Interrupts initialized\n");
    
    register_interrupt_handler(33, &irq1_handler);  // IRQ1 (keyboard) = interrupt 33
    
    ps2_init();
    
    pic_enable_irq(1);
    
    vga_printf("Keyboard enabled\n");

    interrupts_enable();
    
    while(1) __asm__ volatile( "nop" );
    
    __asm__ volatile( "hlt" );
}
