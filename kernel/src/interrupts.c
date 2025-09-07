#include <interrupts/interrupts.h>
#include <idt.h>
#include <pic.h>
#include <vgaio.h>

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

// CPU Exception handlers
void division_error_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Division by Zero Exception (0) ***\n");
    __asm__ volatile("cli; hlt");
}

void debug_exception_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Debug Exception (1) ***\n");
    __asm__ volatile("cli; hlt");
}

void nmi_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Non-Maskable Interrupt (2) ***\n");
    __asm__ volatile("cli; hlt");
}

void breakpoint_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Breakpoint Exception (3) ***\n");
    __asm__ volatile("cli; hlt");
}

void overflow_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Overflow Exception (4) ***\n");
    __asm__ volatile("cli; hlt");
}

void bound_range_exceeded_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Bound Range Exceeded (5) ***\n");
    __asm__ volatile("cli; hlt");
}

void invalid_opcode_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Invalid Opcode Exception (6) ***\n");
    __asm__ volatile("cli; hlt");
}

void device_not_available_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Device Not Available (7) ***\n");
    __asm__ volatile("cli; hlt");
}

void double_fault_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Double Fault Exception (8) ***\n");
    __asm__ volatile("cli; hlt");
}

void invalid_tss_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Invalid TSS Exception (10) ***\n");
    __asm__ volatile("cli; hlt");
}

void segment_not_present_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Segment Not Present (11) ***\n");
    __asm__ volatile("cli; hlt");
}

void stack_segment_fault_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Stack Segment Fault (12) ***\n");
    __asm__ volatile("cli; hlt");
}

void general_protection_fault_handler(void) {
    vga_printf("\n*** KERNEL PANIC: General Protection Fault (13) ***\n");
    vga_printf("This is likely caused by: privilege violation, invalid segment access, or bad interrupt call\n");
    __asm__ volatile("cli; hlt");
}

void page_fault_handler(void) {
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r" (cr2));
    vga_printf("\n*** KERNEL PANIC: Page Fault Exception (14) ***\n");
    vga_printf("Fault address: 0x%llx\n", cr2);
    __asm__ volatile("cli; hlt");
}

void x87_floating_point_handler(void) {
    vga_printf("\n*** KERNEL PANIC: x87 Floating Point Exception (16) ***\n");
    __asm__ volatile("cli; hlt");
}

void alignment_check_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Alignment Check Exception (17) ***\n");
    __asm__ volatile("cli; hlt");
}

void machine_check_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Machine Check Exception (18) ***\n");
    __asm__ volatile("cli; hlt");
}

void simd_floating_point_handler(void) {
    vga_printf("\n*** KERNEL PANIC: SIMD Floating Point Exception (19) ***\n");
    __asm__ volatile("cli; hlt");
}

void virtualization_exception_handler(void) {
    vga_printf("\n*** KERNEL PANIC: Virtualization Exception (20) ***\n");
    __asm__ volatile("cli; hlt");
}