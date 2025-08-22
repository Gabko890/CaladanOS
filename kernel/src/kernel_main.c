#include <stdint.h>
#include <stddef.h>

#include <portio.h>
#include <vgaio.h>

void memcpy(void* src, void* dest, size_t size) {
  for (size_t i = 0; i < size; i++) {
    ((char*)dest)[i] = ((char*)src)[i];
  }
}

char * itoa(int value, char* str, int base){
    char * rc;
    char * ptr;
    char * low;
    // Check for supported base.
    if ( base < 2 || base > 36 )
    {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    // Set '-' for negative decimals.
    if ( value < 0 && base == 10 )
    {
        *ptr++ = '-';
    }
    // Remember where the numbers start.
    low = ptr;
    // The actual conversion.
    do
    {
        // Modulo is negative for negative value. This trick makes abs() unnecessary.
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
        value /= base;
    } while ( value );
    // Terminating the string.
    *ptr-- = '\0';
    // Invert the numbers.
    while ( low < ptr )
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

void kernel_main(uint32_t magic, uint32_t mb2_info) {
    vga_attr(0x0B);
    vga_puts("CaladanOS");
    vga_attr(0x07);
    vga_puts(" loaded                 \n\n");

    vga_printf("kernel at: 0x%X\n", (int)&kernel_main);

    vga_printf("multiboot2 info:\n"
               "    magic: 0x%X\n"
               "    tables at: 0x%X (physical)\n\n",
               magic, mb2_info);

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
    
    vga_printf("cr3 chnged to: 0x%X\n", cr3_value);

    __asm__ volatile( "cli" );
    __asm__ volatile( "hlt" );
}
