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

static struct memory_info minfo;

static void get_available_memory(u32 mb2_info, struct memory_info* minfo) {
    if (!minfo) return;
    
    u64 last_module_end = 0;
    struct multiboot_tag *tag;
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;
            if (last_module_end < module->mod_end) {
                last_module_end = module->mod_end;
            }
        }
    }
    
    struct multiboot_tag_mmap *mmap_tag = 
        (struct multiboot_tag_mmap*)multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_MMAP);
    
    minfo->count = 0;
    if (!mmap_tag) return;
    
    u8* entry_ptr = (u8*)&mmap_tag->entries[0];
    u32 entry_size = mmap_tag->entry_size;
    u32 total_size = mmap_tag->size - sizeof(struct multiboot_tag_mmap);
    u8 count = 0;
    
    for (u32 offset = 0; offset < total_size && count < MEMORY_INFO_MAX; offset += entry_size) {
        struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)(entry_ptr + offset);
        
        if (entry->type != MULTIBOOT_MEMORY_AVAILABLE) continue;
        if (entry->addr == 0x00) continue;
        
        u64 start_addr = entry->addr;
        u64 end_addr = entry->addr + entry->len - 1;
        if (start_addr == 0x100000) {
            u64 kernel_end = (u64)__kernel_end_lma;
            start_addr = (kernel_end > last_module_end) ? kernel_end : last_module_end;
        }
        
        if (start_addr < end_addr) {
            minfo->regions[count].addr_start = start_addr;
            minfo->regions[count].addr_end = end_addr;
            minfo->regions[count].size = end_addr - start_addr + 1;
            minfo->regions[count].flags = MEMORY_INFO_SYSTEM_RAM;
            count++;
        }
    }
    
    minfo->count = count;
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

    get_available_memory(mb2_info, &minfo);
    
    vga_printf("=== Available Memory Regions ===\n");
    for (u8 i = 0; i < minfo.count; i++) {
        vga_printf("Region %d: 0x%llx - 0x%llx (%llu KB)\n", 
                  i + 1, 
                  minfo.regions[i].addr_start,
                  minfo.regions[i].addr_end,
                  minfo.regions[i].size / 1024);
    }
    vga_printf("Available regions: %d\n", minfo.count);

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
    */
    while(1) __asm__ volatile( "nop" );
    
    __asm__ volatile( "hlt" );
}
