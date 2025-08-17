#include <stdint.h>
#include <stddef.h>

#include <portio.h>

void memcpy(void* src, void* dest, size_t size) {
  for (size_t i = 0; i < size; i++) {
    ((char*)dest)[i] = ((char*)src)[i];
  }
}

typedef struct {
  uint8_t x;
  uint8_t y;
} Cursor;

Cursor cursor = {0, 0};
uint8_t color = 0x07;

void putchar(char c) {
  int relative_pos = (cursor.y * 80 + cursor.x) * 2;
  volatile char *video = (volatile char*) 0xb8000;

  if (c != '\n') {
    video[relative_pos] = c;
    video[relative_pos + 1] = color;
    cursor.x++;
  }

  if (cursor.x > 80 || c == '\n') {
    cursor.x = 0;
    cursor.y++;
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

int write_string(const char *string) {
  //static Cursor cursor = {0, 0};
  //static uint8_t color = 0x07;

  if (!string)
    return 1;

  volatile char *video = (volatile char*)0xB8000;

  while( *string != 0 ) {
    if (*string != '\n') {
      int relative_pos = (cursor.y * 80 + cursor.x) * 2;
      video[relative_pos] = *string++;
      video[relative_pos + 1] = color;
      cursor.x++;
    } else {
      *string++;
    }
      
    if (cursor.x > 80 || *string == '\n') {
      cursor.x = 0;
      cursor.y++;
    }
  }

  return 0;
}

void kernel_main(uint32_t magic, uint32_t mb2_info) {
    color = 0x0B;
    write_string("CaladanOS");
    color = 0x07;
    write_string(" loaded                 \n\n");

    write_string("kernel at: 0x");
    char buff[256];
    itoa((int)&kernel_main, buff, 16);
    write_string(buff);
    putchar('\n');

    itoa(magic, buff, 16);
    write_string("multiboot2 info:\n");
    write_string("    magic: 0x");
    write_string(buff);
    putchar('\n');
    
    itoa(mb2_info, buff, 16);
    write_string("    table adress: 0x");
    write_string(buff);
    putchar('\n');
    putchar('\n');

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
    
    write_string("cr3 chnged to: 0x");
    itoa(cr3_value, buff, 16);
    write_string(buff);

    __asm__ volatile( "cli" );
    __asm__ volatile( "hlt" );
}
