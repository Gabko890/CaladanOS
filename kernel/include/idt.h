#include <cldtypes.h>
#include <cldattrs.h>

struct A_PACKED idt_entry_t {
    u16 offset_low;     // bits 0..15 of handler address
    u16 selector;       // code segment selector (usually 0x08 for kernel CS)
    u8  ist;            // bits 0..2 = IST, rest = 0
    u8  type_attr;      // type and attributes
    u16 offset_mid;     // bits 16..31 of handler address
    u32 offset_high;    // bits 32..63 of handler address
    u32 zero;           // reserved
};

struct A_PACKED idt_ptr_t {
    u16 limit;
    u64 base;
};


#define IDT_ENTRIES 256

void set_idt_entry(int vec, void (*handler)(void), u16 selector, u8 flags);
void idt_load(void);
