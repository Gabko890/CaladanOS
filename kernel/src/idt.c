#include <cldtypes.h>
#include <idt.h>

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr   idtr;

void set_idt_entry(int vec, void (*handler)(), u16 selector, u8 flags) {
    if (vec < 0 || vec >= IDT_ENTRIES) {
        return; // Bounds check
    }
    
    u64 addr = (u64)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = selector;
    idt[vec].ist         = 0;
    idt[vec].type_attr   = flags;
    idt[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero        = 0;
}

void idt_load() {
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (u64)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
