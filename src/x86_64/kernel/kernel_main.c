#include <stdint.h>
#include <stddef.h>

#include <paging.h>

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

int putchar(char c) {
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

void test_pages(void) {
    write_string("Testing paging\n");

    char* physical_ptr = (char*)0x01000000;

    map_page((void*)physical_ptr, (void*)physical_ptr, PAGE_WRITABLE);

    physical_ptr[0] = 'O';
    physical_ptr[1] = 'K';

    unmap_page((void*)physical_ptr);

    char* virt_ptr = (char*)0x02000000;

    map_page((void*)virt_ptr, (void*)physical_ptr, PAGE_WRITABLE);

    putchar(virt_ptr[0]); // should print 'O'
    putchar(virt_ptr[1]); // should print 'K'
    
    putchar('\n');
    unmap_page(virt_ptr);
}

void kernel_main(uint32_t magic, uint32_t mb2_info) {
    color = 0x0B;
    write_string("CaladanOS");
    
    color = 0x07;
    write_string(" kernel loaded\n\n");

    char buff[256];
    itoa(magic, buff, 16);

    write_string("bootloader passed:\n");
    write_string("                  magic: 0x");
    write_string(buff);
    putchar('\n');

    itoa(mb2_info, buff, 16);
    write_string("                  mb2_info (physical adress): 0x");
    write_string(buff);
    putchar('\n');

    __asm__ volatile("cli");
    __asm__ volatile("hlt");
}

