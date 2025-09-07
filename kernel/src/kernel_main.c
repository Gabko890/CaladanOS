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
#include <cldtest.h>
#include <dlmalloc/malloc.h>
#include <kmalloc.h>
#include <string.h>
#include "../drivers/cldramfs/cldramfs.h"
#include "../drivers/cldramfs/shell.h"
#include "../drivers/cldramfs/tty.h"
#include <syscalls.h>
#include <syscall_test.h>
#include <elf_loader.h>
#include <process.h>

// Shell integration globals
static int shell_active = 0;

// Key handler for shell
void shell_key_handler(u8 scancode, int is_extended, int is_pressed) {
    if (shell_active && tty_global_handle_key(scancode, is_extended)) {
        // Line is ready, process it
        cldramfs_shell_handle_input();
    }
}

void handle_ps2(void) {
    ps2_handler();
    pic_send_eoi(1); // Send EOI for IRQ1
}

// External declaration for syscall interrupt handler (from assembly)
extern void syscall_interrupt_handler(void);

// Function to load CPIO archive from multiboot modules
static int load_ramfs_from_modules(u32 mb2_info) {
    struct multiboot_tag *tag;
    
    // Look for ramfs.cpio module
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;
            
            // Check if this is the ramfs module
            if (strstr(module->cmdline, "ramfs")) {
                vga_printf("Loading ramfs from module: %s (%u bytes)\n", 
                          module->cmdline, module->mod_end - module->mod_start);
                
                // Load CPIO archive
                int result = cldramfs_load_cpio((void*)(uintptr_t)module->mod_start, 
                                              module->mod_end - module->mod_start);
                if (result == 0) {
                    vga_printf("Ramfs loaded successfully\n");
                    return 0;
                } else {
                    vga_printf("Failed to load ramfs: error %d\n", result);
                    return -1;
                }
            }
        }
    }
    
    vga_printf("No ramfs.cpio module found\n");
    return -1;
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

    // vga_printf("bootloader magic: 0x%X\n", magic);

    multiboot2_parse(magic, mb2_info);
    // multiboot2_print_basic_info(mb2_info);
    // multiboot2_print_modules(mb2_info);



    struct memory_info minfo = get_available_memory(mb2_info);
    
    // dbg_reg_print(&minfo);
    
    // Align kernel end up to next 4KB boundary and add 4KB gap for safety
    u64 kernel_end_aligned = ((u64)__kernel_end_vma + 0xFFFULL) & ~0xFFFULL;
    u64 page_table_virt = kernel_end_aligned + 0x1000ULL;  // Add 4KB gap
    u64 pml4_phys = mm_init(&minfo, page_table_virt);
    
    if (0x00 == pml4_phys) {
        vga_printf("memory mapper initialization failed\n");
        __asm__ volatile("cli; hlt");
    }

    // Identity mapping - using physical page table access
    for (uint64_t addr = 0; addr < (16ULL << 30); addr += (2ULL << 20)) {
        if (!mm_map(addr, addr, PTE_RW | PTE_HUGE, PAGE_2M)) {
            vga_printf("identity map failure at 0x%llx\n", addr);
            __asm__ volatile("cli; hlt");
        }
    }

    // Kernel virtual mapping - using physical page table access
    uint64_t kernel_phys = 0x00200000ULL;   // KERNEL_PMA
    uint64_t kernel_virt = 0xFFFFFFFF80000000ULL; // KERNEL_VMA
    uint64_t kernel_size = 4ULL << 20; // e.g. 4 MiB kernel

    for (uint64_t off = 0; off < kernel_size; off += 0x1000) {
        if (!mm_map(kernel_virt + off, kernel_phys + off, PTE_RW | PTE_PRESENT, PAGE_4K)) {
            vga_printf("kernel heap map failure\n");
            __asm__ volatile("cli; hlt");
        }
    }


    // Switch to new page tables
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(pml4_phys)
        : "memory"
    );

    // Enable virtual access to page tables
    if (!mm_enable_virtual_tables()) {
        vga_printf("failed to enable virtual page tables\n");
        __asm__ volatile("cli; hlt");
    }

    // Initialize kmalloc heap now that virtual mapping is complete
    kmalloc_init(&minfo);
    vga_printf("Dynamic memory allocator initialized\n");
        
    extern void irq1_handler(void);

    // interrupt system (PIC + IDT)
    pic_init();
    
    // Set up specific CPU exception handlers (0-31)
    set_idt_entry(0, &division_error_handler, 0x08, 0x8e);              // Division by zero
    set_idt_entry(1, &debug_exception_handler, 0x08, 0x8e);             // Debug
    set_idt_entry(2, &nmi_handler, 0x08, 0x8e);                         // NMI
    set_idt_entry(3, &breakpoint_handler, 0x08, 0x8e);                  // Breakpoint
    set_idt_entry(4, &overflow_handler, 0x08, 0x8e);                    // Overflow
    set_idt_entry(5, &bound_range_exceeded_handler, 0x08, 0x8e);        // Bound range exceeded
    set_idt_entry(6, &invalid_opcode_handler, 0x08, 0x8e);              // Invalid opcode
    set_idt_entry(7, &device_not_available_handler, 0x08, 0x8e);        // Device not available
    set_idt_entry(8, &double_fault_handler, 0x08, 0x8e);                // Double fault
    // Skip 9 (coprocessor segment overrun - obsolete)
    set_idt_entry(10, &invalid_tss_handler, 0x08, 0x8e);                // Invalid TSS
    set_idt_entry(11, &segment_not_present_handler, 0x08, 0x8e);        // Segment not present
    set_idt_entry(12, &stack_segment_fault_handler, 0x08, 0x8e);        // Stack segment fault
    set_idt_entry(13, &general_protection_fault_handler, 0x08, 0x8e);   // General protection fault
    set_idt_entry(14, &page_fault_handler, 0x08, 0x8e);                 // Page fault
    // Skip 15 (reserved)
    set_idt_entry(16, &x87_floating_point_handler, 0x08, 0x8e);         // x87 FPU error
    set_idt_entry(17, &alignment_check_handler, 0x08, 0x8e);            // Alignment check
    set_idt_entry(18, &machine_check_handler, 0x08, 0x8e);              // Machine check
    set_idt_entry(19, &simd_floating_point_handler, 0x08, 0x8e);        // SIMD FPU error
    set_idt_entry(20, &virtualization_exception_handler, 0x08, 0x8e);   // Virtualization
    
    // Set remaining exception handlers (21-31) to default
    for (int i = 21; i < 32; i++) {
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    // Set up PIC interrupt handlers (32-47)
    for (int i = 32; i < 48; i++) {
        set_idt_entry(i, &default_interrupt_handler, 0x08, 0x8e);
    }
    
    idt_load();
    
    // Set up syscall interrupt (0x80)
    set_idt_entry(0x80, &syscall_interrupt_handler, 0x08, 0x8E);  // 0x8E = DPL 0 (kernel accessible)
    
    // Set up process exit interrupt (INT 3 - breakpoint)
    set_idt_entry(3, &default_interrupt_handler, 0x08, 0x8E);
    
    // Initialize syscall system
    syscalls_init();
    
    // Initialize process management system
    process_init();
    
    register_interrupt_handler(33, &irq1_handler);  // IRQ1 (keyboard) = interrupt 33
    
    ps2_init();
    
    pic_enable_irq(1);
    
    vga_printf("Interrupts initialized\n");
    interrupts_enable();
    vga_printf("Keyboard enabled\n");
    
    // Test syscall system (using direct calls)
    test_syscalls();
    
    // Test 0x80 interrupt directly from kernel code
    /*vga_printf("[TEST] Testing int 0x80 directly from kernel...\n");
    long result_direct = 0;
    __asm__ volatile (
        "mov $20, %%rax\n\t"        // getpid syscall number
        "int $0x80\n\t"             // trigger interrupt
        "mov %%rax, %0\n\t"         // get result
        : "=m" (result_direct)
        :
        : "rax", "memory"
    );
    vga_printf("[TEST] int 0x80 from kernel returned: %ld\n", result_direct);
    */

    CLDTEST_INIT();
    
    // Run all tests:
    //CLDTEST_RUN_ALL();
    
    //vga_printf("Running memory tests...\n");
    CLDTEST_RUN_SUITE("memory_tests");
    //vga_printf("Running malloc tests...\n");
    CLDTEST_RUN_SUITE("malloc_tests");
    //vga_printf("Running cldramfs tests...\n");
    CLDTEST_RUN_SUITE("cldramfs_tests");
    //vga_printf("Running tty tests...\n");
    CLDTEST_RUN_SUITE("tty_tests");
    
    vga_printf("\n=== SYSTEM READY ===\n");
    
    // Initialize and start ramfs shell
    vga_printf("Initializing ramfs shell...\n");
    
    // Set up keyboard callback for shell
    ps2_set_key_callback(shell_key_handler);
    
    // Initialize ramfs first
    cldramfs_init();
    
    // Try to load ramfs from multiboot modules
    if (load_ramfs_from_modules(mb2_info) == 0) {
        // ELF loader testing disabled during boot - use shell instead
        vga_printf("ELF loader ready - test with 'exec' command in shell\n");
        
        // Initialize shell
        cldramfs_shell_init();
        shell_active = 1;
        
        //vga_printf("\n=== SHELL ACTIVE ===\n");
        vga_attr(0x07);
        
        // Main shell loop
        while(cldramfs_shell_is_running()) {
            __asm__ volatile("hlt"); // Wait for interrupts
        }
        
        vga_printf("Shell exited\n");
    } else {
        vga_printf("Failed to load ramfs, continuing without shell\n");
        vga_attr(0x07);
        
        while(1) __asm__ volatile("hlt");
    }
}
