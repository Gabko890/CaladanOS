#include <multiboot/multiboot2.h>
#include <vgaio.h>
#include <stddef.h>
#include <stdint.h>

int multiboot2_parse(uint32_t magic, uint32_t mb2_info) {
    if (magic != MULTIBOOT2_MAGIC) {
        return -1;  // Invalid magic, return error
    }
    
    // Just validate the multiboot2 info is accessible
    // No printing, just return success/failure
    return 0;
}

void multiboot2_print_basic_info(uint32_t mb2_info) {
    struct multiboot_tag *tag;
    
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        switch (tag->type) {
            case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME: {
                struct multiboot_tag_string *bootloader = (struct multiboot_tag_string*)tag;
                vga_printf("Bootloader: %s\n", bootloader->string);
                break;
            }
            case MULTIBOOT_TAG_TYPE_MODULE: {
                struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;
                vga_printf("Module: %s (%u bytes) at 0x%X-0x%X\n", 
                          module->cmdline, module->mod_end - module->mod_start,
                          module->mod_start, module->mod_end);
                break;
            }
        }
    }
}

struct multiboot_tag* multiboot2_find_tag(uint32_t mb2_info, uint32_t type) {
    struct multiboot_tag *tag;
    
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == type) {
            return tag;
        }
    }
    
    return NULL;
}

void multiboot2_print_modules(uint32_t mb2_info) {
    struct multiboot_tag *tag;
    int module_count = 0;
    
    vga_printf("=== Loaded Modules ===\n");
    
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;
            module_count++;
            
            vga_printf("Module %d:\n", module_count);
            vga_printf("  Start: 0x%X\n", module->mod_start);
            vga_printf("  End:   0x%X\n", module->mod_end);
            vga_printf("  Size:  %u bytes\n", module->mod_end - module->mod_start);
            vga_printf("  Name:  %s\n", module->cmdline);
            vga_printf("\n");
        }
    }
    
    if (module_count == 0) {
        vga_printf("No modules found.\n");
    } else {
        vga_printf("Total modules: %d\n", module_count);
    }
    vga_printf("======================\n");
}

struct multiboot_tag_mmap* multiboot2_get_memory_map(uint32_t mb2_info) {
    struct multiboot_tag *tag = multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_MMAP);
    
    if (tag == NULL) {
        return NULL;
    }
    
    return (struct multiboot_tag_mmap*)tag;
}

size_t multiboot2_get_memory_map_entries(struct multiboot_tag_mmap* mmap_tag) {
    if (mmap_tag == NULL) {
        return 0;
    }
    
    return (mmap_tag->size - sizeof(struct multiboot_tag_mmap)) / mmap_tag->entry_size;
}

static void print_padded_hex(uint64_t addr) {
    uint32_t hi = (uint32_t)(addr >> 32);
    uint32_t lo = (uint32_t)(addr & 0xFFFFFFFF);
    
    vga_printf("0x%lX", ((uint64_t)hi << 32) | lo);
}

const char* multiboot2_memory_type_to_string(uint32_t type) {
    switch (type) {
        case MULTIBOOT_MEMORY_AVAILABLE:
            return "Available";
        case MULTIBOOT_MEMORY_RESERVED:
            return "Reserved";
        case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
            return "ACPI Reclaimable";
        case MULTIBOOT_MEMORY_NVS:
            return "ACPI NVS";
        case MULTIBOOT_MEMORY_BADRAM:
            return "Bad RAM";
        default:
            return "Unknown";
    }
}

void multiboot2_print_memory_map(uint32_t mb2_info) {
    struct multiboot_tag_mmap *mmap_tag = multiboot2_get_memory_map(mb2_info);
    
    if (mmap_tag == NULL) {
        vga_printf("No memory map found in multiboot info.\n");
        return;
    }
    
    vga_printf("\n=== Memory Map ===\n");
    vga_printf("Entry size: %u bytes\n", mmap_tag->entry_size);
    vga_printf("Entry version: %u\n", mmap_tag->entry_version);
    vga_printf("\n");
    
    size_t num_entries = multiboot2_get_memory_map_entries(mmap_tag);
    
    vga_printf("Start            End              Size (KB)  Type\n");
    vga_printf("----------------------------------------------------------------\n");
    
    for (size_t i = 0; i < num_entries; i++) {
        struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
        uint64_t end_addr = entry->addr + entry->len - 1;
        uint64_t size_kb = entry->len / 1024;
        
        print_padded_hex(entry->addr);
        vga_printf("-");
        print_padded_hex(end_addr);
        vga_printf(" %luKB %s\n", size_kb, multiboot2_memory_type_to_string(entry->type));
    }
    
    vga_printf("Total entries: %zu\n", num_entries);
    vga_printf("==================\n");
}

void multiboot2_get_modules(uint32_t mb2_info, struct mb2_modules_list* result) {
    struct multiboot_tag *tag;
    
    if (!result) return;
    
    result->count = 0;
    
    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;
            
            if (result->count < 32) {
                result->modules[result->count] = module;
                result->count++;
            }
        }
    }
}

struct mb2_memory_map multiboot2_get_memory_regions(uint32_t mb2_info) {
    struct mb2_memory_map result = {0};
    struct multiboot_tag_mmap *mmap_tag = multiboot2_get_memory_map(mb2_info);
    
    if (mmap_tag == NULL) {
        return result;
    }
    
    size_t num_entries = multiboot2_get_memory_map_entries(mmap_tag);
    if (num_entries > MB2_MAX_MEMORY_REGIONS) {
        num_entries = MB2_MAX_MEMORY_REGIONS;
    }
    
    for (size_t i = 0; i < num_entries; i++) {
        struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
        
        result.regions[i].start_addr = entry->addr;
        result.regions[i].end_addr = entry->addr + entry->len - 1;
        result.regions[i].size = entry->len;
        result.regions[i].type = entry->type;
    }
    
    result.count = num_entries;
    return result;
}
