#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;     // bits 0..15 of handler address
    uint16_t selector;       // code segment selector (usually 0x08 for kernel CS)
    uint8_t  ist;            // bits 0..2 = IST, rest = 0
    uint8_t  type_attr;      // type and attributes
    uint16_t offset_mid;     // bits 16..31 of handler address
    uint32_t offset_high;    // bits 32..63 of handler address
    uint32_t zero;           // reserved
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));


#define IDT_ENTRIES 256

void set_idt_entry(int vec, void (*handler)(), uint16_t selector, uint8_t flags);
void idt_load();
