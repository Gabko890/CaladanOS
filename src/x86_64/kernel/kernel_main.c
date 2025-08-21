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

void kernel_main() {
    write_string("CaladanOS kernel loaded\n\n");

    write_string("Testing paging\n");

    // pick 16 MiB physical base (aligned to 2 MiB)
    char* physical_ptr = (char*)0x01000000;  

    // map physical to itself
    map_page((void*)physical_ptr, (void*)physical_ptr, PAGE_WRITABLE);

    physical_ptr[0] = 'O';
    physical_ptr[1] = 'K';

    // unmap it
    unmap_page((void*)physical_ptr);

    // map it somewhere else, e.g. 32 MiB (aligned)
    char* virt_ptr = (char*)0x02000000;

    map_page((void*)virt_ptr, (void*)physical_ptr, PAGE_WRITABLE);

    putchar(virt_ptr[0]); // should print 'O'
    putchar(virt_ptr[1]); // should print 'K'

    __asm__ volatile("cli");
    __asm__ volatile("hlt");
}

