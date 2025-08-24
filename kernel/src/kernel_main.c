#include <stdint.h>
#include <stddef.h>

#include <portio.h>
#include <vgaio.h>
#include <interrupts/interrupts.h>
#include <ps2.h>
#include <pic.h>
#include <multiboot/multiboot2.h>

#include <ram_manager/ram_manager.h>

static struct mb2_modules_list g_modules;

void handle_ps2() {
    ps2_handler();
    pic_send_eoi(1); // Send EOI for IRQ1
}

void kernel_main(uint32_t magic, uint32_t mb2_info) {
    vga_attr(0x0B);
    vga_puts("CaladanOS");
    vga_attr(0x07);
    vga_puts(" loaded        \n\n");
    
    extern char _end;
    vga_printf("kernel at: 0x%X - 0x%X\n", (int)&kernel_main, &_end);
    
    multiboot2_parse(magic, mb2_info);
    multiboot2_print_basic_info(mb2_info);
    multiboot2_print_memory_map(mb2_info);


    struct mb2_memory_map m = {0};
    multiboot2_get_modules(mb2_info, &g_modules);
    multiboot2_get_memory_regions(mb2_info, &m);
    vga_printf("ramtable will be placed at: 0x%X\n", init_ram_manager(g_modules, m));

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
    
    //vga_printf("cr3 chnged to: 0x%X\n", cr3_value);
    
    extern void irq1_handler();

    // interrupt system (PIC + IDT)
    /*interrupts_init();
    vga_printf("Interrupts initialized\n");
    
    register_interrupt_handler(33, &irq1_handler);  // IRQ1 (keyboard) = interrupt 33
    
    ps2_init();
    
    pic_enable_irq(1);
    
    vga_printf("Keyboard enabled\n");

    interrupts_enable();
    */ 
    while(1) __asm__ volatile( "nop" );
    
    __asm__ volatile( "hlt" );
}
