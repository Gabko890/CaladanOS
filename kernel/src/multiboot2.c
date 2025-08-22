#include <multiboot/multiboot2.h>
#include <vgaio.h>
#include <stddef.h>
#include <stdint.h>

void multiboot2_parse(uint32_t magic, uint32_t mb2_info) {
    if (magic != MULTIBOOT2_MAGIC) {
        vga_printf("Invalid multiboot2 magic: 0x%X\n", magic);
        return;
    }
    
    // Parse tags - only show bootloader and modules
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
    
    vga_printf("\n=== Loaded Modules ===\n");
    
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
    vga_printf("======================\n\n");
}
