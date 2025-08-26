#include <stddef.h>
#include <cldtypes.h>

#include <vgaio.h>
#include <multiboot/multiboot2.h>
#include <ldinfo.h>


void get_memory_info(
    struct mb2_memory_map* mmap,
    struct mb2_modules_list* mlist,
    void* outstruct
) {
    // debug to print avialable memory
    for(u8 i = 0; i < mmap->count; i++) {
        struct mb2_memory_region current_region = mmap->regions[i];
        
        if (MULTIBOOT_MEMORY_AVAILABLE != current_region.type) continue;
        vga_printf("region: %d\n", (int)i);

    }
}
