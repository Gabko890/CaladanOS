#include <cldtypes.h>
#include <portio.h>
#include <vgaio.h>
#include <interrupts/interrupts.h>
#include <ps2.h>
#include <pic.h>
#include <idt.h>
#include <multiboot/multiboot2.h>
#include <memory_info.h>
#include <ldinfo.h>

#include <memory_mapper.h>

void handle_ps2(void) {
    ps2_handler();
    pic_send_eoi(1); // Send EOI for IRQ1
}

static void dbg_reg_print(struct memory_info* minfo) {
    vga_printf("=== Available Memory Regions ===\n");
    for (u8 i = 0; i < minfo->count; i++) {
        vga_printf("Region %d: 0x%llx - 0x%llx (%llu KB)\n",
                  i + 1,
                  minfo->regions[i].addr_start,
                  minfo->regions[i].addr_end,
                  minfo->regions[i].size / 1024);
    }
    vga_printf("Available regions: %d\n", minfo->count);
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

    //multiboot2_parse(magic, mb2_info);
    //multiboot2_print_basic_info(mb2_info);
    //multiboot2_print_memory_map(mb2_info);
    //multiboot2_print_modules(mb2_info);



    struct memory_info minfo = get_available_memory(mb2_info);
    
    dbg_reg_print(&minfo);
    
    u64 pml4_phys = mm_init(&minfo /*, (void*)0xFFFFFFFF80400000UL*/);
    
    if (0x00 == pml4_phys) {
        vga_printf("kernel map failure 1");
        __asm__ volatile( "cli; hlt" );
    }
    vga_printf("pml_4 at: %llx\n", pml4_phys);
    
    dbg_reg_print(&minfo);

    vga_printf("Starting identity mapping...\n");
    for (uint64_t addr = 0; addr < (16ULL << 30); addr += (2ULL << 20)) {
        if (addr < (4ULL << 20)) vga_printf("Mapping identity: 0x%llx\n", addr);
        if (!mm_map(addr, addr, PTE_RW | PTE_HUGE, PAGE_2M)) {
            vga_printf("identity map failure at 0x%llx\n", addr);
            __asm__ volatile("cli; hlt");
        }
        if (addr == 0) vga_printf("First identity map succeeded\n");
    }

    uint64_t kernel_phys = 0x00200000ULL;   // KERNEL_PMA
    uint64_t kernel_virt = 0xFFFFFFFF80000000ULL; // KERNEL_VMA
    uint64_t kernel_size = 4ULL << 20; // e.g. 4 MiB kernel

    for (uint64_t off = 0; off < kernel_size; off += 0x1000) {
        if (!mm_map(kernel_virt + off, kernel_phys + off, PTE_RW | PTE_PRESENT, PAGE_4K)) {
            vga_printf("kernel map failure2\n");
            __asm__ volatile("cli; hlt");
        }
    }

    vga_printf("About to switch CR3...\n");

    __asm__ volatile (
        "mov %0, %%cr3\n\t"
        "invlpg (%%rip)"
        :
        : "r"(pml4_phys)
        : "memory"
    );
    
    vga_printf("cr3 chnged to: 0x%X\n", pml4_phys);
        
    extern void irq1_handler(void);

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
