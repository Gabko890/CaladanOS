#include <cldtypes.h>
#include <stddef.h>

#include <portio.h>
#include <vgaio.h>
#include <interrupts/interrupts.h>
#include <ps2.h>
#include <pic.h>
#include <idt.h>
#include <multiboot/multiboot2.h>
#include <memory_info.h>
#include <ldinfo.h>

void handle_ps2() {
    ps2_handler();
    pic_send_eoi(1); // Send EOI for IRQ1
}


void kernel_main(volatile u32 magic, u32 mb2_info) {
    vga_attr(0x0B);
    vga_printf("CaladanOS");
    vga_attr(0x07);
    vga_printf(" loaded        \n\n"); 
    
    vga_printf("Boot stub:\n  VMA=0x%llx = 0x%llx\n  LMA=0x%llx - 0x%llx\n",
           __boot_start_vma, __boot_end_vma,
           __boot_start_lma, __boot_end_lma);

    vga_printf("Kernel:\n  VMA=0x%llx - 0x%llx\n  LMA=0x%llx - 0x%llx\n\n",
           __kernel_start_vma, __kernel_end_vma,
           __kernel_start_lma, __kernel_end_lma);

    vga_printf("bootloader magic: 0x%X\n", magic);

    multiboot2_parse(magic, mb2_info);
    multiboot2_print_basic_info(mb2_info);
    multiboot2_print_memory_map(mb2_info);
    //multiboot2_print_modules(mb2_info);

    struct mb2_memory_map mb_mmap;
    struct mb2_modules_list mb_modules;
    
    multiboot2_get_modules(mb2_info, &mb_modules);
    multiboot2_get_memory_regions(mb2_info, &mb_mmap);
    
    struct memory_info minfo;
    get_memory_info(&mb_mmap, &mb_modules, NULL); // passing &minfo causes kernel to crash 

    /*
    extern void setup_page_tables();
    setup_page_tables();
    
    unsigned long cr3_value = 0x10000;

    __asm__ volatile (
        "mov %0, %%cr3\n\t"
        "invlpg (%%rip)"
        :
        : "r"(cr3_value)
        : "memory"
    );
    
    vga_printf("cr3 chnged to: 0x%X\n", cr3_value);
    */ 
    
    extern void irq1_handler();

    // interrupt system (PIC + IDT)
    pic_init();
    
    // Set up exception handlers (0-31)
    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    // Set up PIC interrupt handlers (32-47)
    for (int i = 32; i < 48; i++) {
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    idt_load();
    
    register_interrupt_handler(33, &irq1_handler);  // IRQ1 (keyboard) = interrupt 33
    
    ps2_init();
    
    pic_enable_irq(1);
    
    vga_printf("Interrupts initialized\n");
    interrupts_enable();
    vga_printf("Keyboard enabled\n");
    while(1) __asm__ volatile( "nop" );
    
    __asm__ volatile( "hlt" );
}
