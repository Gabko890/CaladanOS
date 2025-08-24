#include <ram_manager/ram_manager.h>
#include <ram_manager/available.h>
#include <multiboot/multiboot2.h>
#include <stdint.h>

#include <vgaio.h>

uint64_t init_ram_manager(struct mb2_modules_list modules_list, struct mb2_memory_map memory_map) {
    uint64_t first_ram_avialable;

    if (0 != modules_list.count) {
        first_ram_avialable = modules_list.modules[modules_list.count - 1]->mod_end;
    } else {
        extern char _end;
        first_ram_avialable = (uint64_t)&_end;
    }
    
    if (0 == memory_map.count) return 0x00;
    //vga_printf("found %d enties in memory_map\n", memory_map.count);
    
    volatile struct available_map* map = (struct available_map*)first_ram_avialable;
    map->count = 0;

    for (size_t i = 0; i < memory_map.count; i++) {
        struct mb2_memory_region region = memory_map.regions[i];

        if (0 == region.start_addr || region.type != MULTIBOOT_MEMORY_AVAILABLE) 
            continue;
        else if (region.start_addr == 0x100000) 
            region.start_addr = first_ram_avialable + sizeof(struct available_map);
        
        map->entries[i].start_addr = region.start_addr;
        map->entries[i].end_addr   = region.end_addr;
        map->entries[i].size       = region.size;
        map->count++;        
    }

    vga_printf("found %d aviable entries in memory map\n", map->count);

    for(int i = 0; i < map->count; i++) {
        vga_printf("at 0x%X ", map->entries[i].start_addr);
    }
    vga_putchar('\n');

    return first_ram_avialable;
}
