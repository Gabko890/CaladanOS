#include <stdint.h>
#include <stddef.h>

#include <portio.h>
#include <vgaio.h>
#include <interrupts/interrupts.h>
#include <ps2.h>
#include <pic.h>
#include <idt.h>
#include <multiboot/multiboot2.h>

#include <ram_manager/ram_manager.h>

static struct mb2_modules_list g_modules;

void handle_ps2() {
    ps2_handler();
    pic_send_eoi(1); // Send EOI for IRQ1
}

//__attribute__((section(".test"))) int test_var;

void kernel_main(volatile uint32_t magic, uint32_t mb2_info) {
    vga_attr(0x0B);
    vga_printf("CaladanOS");
    vga_attr(0x07);
    vga_printf(" loaded        \n\n"); // those \n are ignore 
    
    extern char _end;
    vga_printf("kernel at: 0x%lX - 0x%lX\n", (uintptr_t)&kernel_main, (uintptr_t)&_end);

    vga_printf("bootloader provided magic: %X\n", magic);

    multiboot2_parse(magic, mb2_info);
    multiboot2_print_basic_info(mb2_info);
    multiboot2_print_memory_map(mb2_info);


    //struct mb2_memory_map m = {0};
    //multiboot2_get_modules(mb2_info, &g_modules);
    //multiboot2_get_memory_regions(mb2_info, &m);
    //vga_printf("ramtable placed at: 0x%X\n", init_ram_manager(g_modules, m));

    extern void setup_page_tables();
    setup_page_tables();
    
    //unsigned long cr3_value = 0x1000;

    //__asm__ volatile (
    //    "mov %0, %%rax\n\t"
    //    "mov %%rax, %%cr3"
    //    :
    //    : "r"(cr3_value)
    //    : "rax"
    //);
    
    //vga_printf("test section at: 0x%llx\n", &test_var);

    //vga_printf("cr3 chnged to: 0x%X\n", cr3_value);
    
    
    extern void irq1_handler();

    // interrupt system (PIC + IDT)
    vga_printf("Initializing interrupts...\n");
    vga_printf("About to call pic_init...\n");
    pic_init();
    vga_printf("PIC initialized\n");
    
    vga_printf("Setting up IDT...\n");
    vga_printf("default_interrupt_handler at: %p\n", (void*)&default_interrupt_handler);
    
    // Only set the essential entries we need instead of all 256
    vga_printf("Setting essential IDT entries...\n");
    
    // Set up exception handlers (0-31)
    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    // Set up PIC interrupt handlers (32-47)
    for (int i = 32; i < 48; i++) {
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    vga_printf("Essential IDT entries set\n");
    
    // Load IDT
    vga_printf("Loading IDT...\n");
    idt_load();
    vga_printf("IDT loaded\n");
    
    vga_printf("Registering IRQ1 handler at: %p\n", (void*)irq1_handler);
    register_interrupt_handler(33, &irq1_handler);  // IRQ1 (keyboard) = interrupt 33
    vga_printf("IRQ1 handler registered\n");
    
    vga_printf("Initializing PS/2...\n");
    ps2_init();
    vga_printf("PS/2 initialized\n");
    
    vga_printf("Enabling IRQ1...\n");
    pic_enable_irq(1);
    vga_printf("IRQ1 enabled\n");
    
    vga_printf("Enabling interrupts globally...\n");
    interrupts_enable();
    vga_printf("Interrupts enabled - keyboard should work now\n");
    while(1) __asm__ volatile( "nop" );
    
    __asm__ volatile( "hlt" );
}
