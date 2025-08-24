#include <ram_manager/ram_manager.h>
#include <ram_manager/available.h>
#include <multiboot/multiboot2.h>
#include <stdint.h>

#include <vgaio.h>

uint64_t init_ram_manager(struct mb2_modules_list modules_list, struct mb2_memory_map memory_map) {
    uint64_t first_ram_avialable;
    
    /* Always calculate kernel physical end */
    extern char _end;
    uint64_t virt_end = (uint64_t)&_end;
    uint64_t kernel_phys_end = virt_end - 0xFFFFFFFF80000000ULL + 0x200000ULL;
    vga_printf("DEBUG: Kernel physical end = 0x%llX\n", kernel_phys_end);

    if (0 != modules_list.count) {
        vga_printf("DEBUG: Using modules, count = %d\n", modules_list.count);
        uint64_t module_end = modules_list.modules[modules_list.count - 1]->mod_end;
        vga_printf("DEBUG: Module end = 0x%llX\n", module_end);
        
        /* Use whichever is higher: kernel end or module end */
        first_ram_avialable = (kernel_phys_end > module_end) ? kernel_phys_end : module_end;
    } else {
        vga_printf("DEBUG: No modules, using kernel _end\n");
        first_ram_avialable = kernel_phys_end;
    }
    
    vga_printf("DEBUG: Final ram location = 0x%llX\n", first_ram_avialable);
    
    if (0 == memory_map.count) return 0x00;
    //vga_printf("found %d enties in memory_map\n", memory_map.count);
    
    volatile struct available_map* map = (struct available_map*)first_ram_avialable;
    map->count = 0;

    for (size_t i = 0; i < memory_map.count; i++) {
        struct mb2_memory_region region = memory_map.regions[i];

        if (region.type != MULTIBOOT_MEMORY_AVAILABLE || 0 == region.start_addr) 
            continue;
        else if (region.start_addr == 0x100000) 
            region.start_addr = first_ram_avialable + sizeof(struct available_map);
        
        map->entries[i].start_addr = region.start_addr;
        map->entries[i].end_addr   = region.end_addr;
        map->entries[i].size       = region.size;
        map->count++;
        
        //vga_printf("available start: 0x%llx end: 0x%llx\n", map->entries[i].start_addr, map->entries[i].end_addr);
    }

    //vga_printf("found %d aviable entries in memory map\n", map->count);

    //for(int i = 0; i < map->count; i++) {
    //    vga_printf("at 0x%X ", map->entries[i].start_addr);
    //}
    //vga_putchar('\n');

    return first_ram_avialable;
}