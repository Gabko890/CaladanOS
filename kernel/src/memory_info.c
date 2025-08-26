#include <stddef.h>
#include <cldtypes.h>
#include <multiboot/multiboot2.h>
#include <ldinfo.h>
#include <memory_info.h>

struct memory_info get_available_memory(u32 mb2_info) {
    struct memory_info result = {0};
    
    u64 last_module_end = 0;
    struct multiboot_tag *tag;
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;
            if (last_module_end < module->mod_end) {
                last_module_end = module->mod_end;
            }
        }
    }
    
    struct multiboot_tag_mmap *mmap_tag = 
        (struct multiboot_tag_mmap*)multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_MMAP);
    
    if (!mmap_tag) return result;
    
    u8* entry_ptr = (u8*)&mmap_tag->entries[0];
    u32 entry_size = mmap_tag->entry_size;
    u32 total_size = mmap_tag->size - sizeof(struct multiboot_tag_mmap);
    u8 count = 0;
    
    for (u32 offset = 0; offset < total_size && count < MEMORY_INFO_MAX; offset += entry_size) {
        struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)(entry_ptr + offset);
        
        if (entry->type != MULTIBOOT_MEMORY_AVAILABLE) continue;
        if (entry->addr == 0x00) continue;
        
        u64 start_addr = entry->addr;
        u64 end_addr = entry->addr + entry->len - 1;
        if (start_addr == 0x100000) {
            u64 kernel_end = (u64)__kernel_end_lma;
            start_addr = (kernel_end > last_module_end) ? kernel_end : last_module_end;
        }
        
        if (start_addr < end_addr) {
            result.regions[count].addr_start = start_addr;
            result.regions[count].addr_end = end_addr;
            result.regions[count].size = end_addr - start_addr + 1;
            result.regions[count].flags = MEMORY_INFO_SYSTEM_RAM;
            count++;
        }
    }
    
    result.count = count;
    return result;
}
