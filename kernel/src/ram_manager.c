#include <ram_manager/ram_manager.h>
#include <multiboot/multiboot2.h>
#include <stdint.h>

#include <vgaio.h>

uint64_t init_ram_manager(struct mb2_modules_list modules_list, struct mb2_memory_map memory_map) {
    uint64_t first_ram_avialable;

    if (0 != modules_list.count) {
        first_ram_avialable = modules_list.modules[modules_list.count - 1]->mod_end;
    } else {
        extern char _end;
        first_ram_avialable = &_end;
    }
    
    if (0 == memory_map.count) return 0x00;
    vga_printf("found %d enties in memory_map\n", memory_map.count);

    return first_ram_avialable;
}
