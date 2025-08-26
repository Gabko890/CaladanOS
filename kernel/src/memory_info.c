#include <stddef.h>
#include <cldtypes.h>

#include <vgaio.h>
#include <multiboot/multiboot2.h>
#include <ldinfo.h>

#include <memory_info.h>


void get_memory_info(
    struct mb2_memory_map* mmap,
    struct mb2_modules_list* mlist,
    struct memory_info* minfo
) {
    if (!mmap || !mlist || !minfo) return;
    
    u64 last_module_end = 0;
    for (u8 i = 0; i < mlist->count && mlist->modules[i]; i++) {
        struct multiboot_tag_module current_module = *(mlist->modules[i]);
        
        if (last_module_end < current_module.mod_end) { 
            last_module_end = current_module.mod_end;
        }
    }

    vga_printf("last module at: 0x%llx\n", last_module_end);

    
    u8 count = 0;
    for(u8 i = 0; i < mmap->count && count < MEMORY_INFO_MAX; i++) {
        if (i >= MB2_MAX_MEMORY_REGIONS) break;
        
        struct memory_region current_region;
        // Safe copy to avoid alignment issues
        __builtin_memcpy(&current_region, &mmap->regions[i], sizeof(struct memory_region));
        
        if (MULTIBOOT_MEMORY_AVAILABLE != current_region.flags) continue;

        if (0x00 == current_region.addr_start) continue;
        else if (0x100000 == current_region.addr_start) {
            current_region.addr_start = (u64)__kernel_end_lma > last_module_end ? 
                                        (u64)__kernel_end_lma : last_module_end ;
        }

        minfo->regions[count].addr_start = current_region.addr_start;
        minfo->regions[count].addr_end   = current_region.addr_end;
        minfo->regions[count].flags = MEMORY_INFO_SYSTEM_RAM;

        vga_printf("aviable region: 0x%llx - 0x%llx\n", current_region.addr_start, current_region.addr_end);

        count++;
    }

    minfo->count = count;
    return;
}
