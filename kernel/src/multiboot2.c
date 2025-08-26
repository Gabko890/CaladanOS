#include <multiboot/multiboot2.h>
#include <vgaio.h>
#include <stddef.h>
#include <cldtypes.h>
#include <limits.h>
#include <string.h>

int multiboot2_parse(u32 magic, u32 mb2_info) {
    (void)mb2_info;
    if (magic != MULTIBOOT2_MAGIC) {
        return -1;  // Invalid magic, return error
    }
    // Just validate the multiboot2 info is accessible
    // No printing, just return success/failure
    return 0;
}

void multiboot2_print_basic_info(u32 mb2_info) {
    struct multiboot_tag *tag;

    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {

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
            default:
                break;
        }
    }
}

struct multiboot_tag* multiboot2_find_tag(u32 mb2_info, u32 type) {
    struct multiboot_tag *tag;

    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {

        if (tag->type == type) {
            return tag;
        }
    }
    return NULL;
}

void multiboot2_print_modules(u32 mb2_info) {
    struct multiboot_tag *tag;
    int module_count = 0;

    vga_printf("=== Loaded Modules ===\n");

    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {

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

struct multiboot_tag_mmap* multiboot2_get_memory_map(u32 mb2_info) {
    struct multiboot_tag *tag = multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_MMAP);
    if (tag == NULL) {
        return NULL;
    }
    return (struct multiboot_tag_mmap*)tag;
}

size_t multiboot2_get_memory_map_entries(struct multiboot_tag_mmap* mmap_tag) {
    if (mmap_tag == NULL || mmap_tag->entry_size == 0) {
        return 0;
    }
    return (mmap_tag->size - sizeof(struct multiboot_tag_mmap)) / mmap_tag->entry_size;
}

// --- Printing helpers (unchanged behaviour) ---
static void print_padded_hex(u64 addr) {
    // Your vga_printf uses %lX for 64-bit? This preserves your original callsite.
    vga_printf("0x%lX", addr);
}

const char* multiboot2_memory_type_to_string(u32 type) {
    switch (type) {
        case MULTIBOOT_MEMORY_AVAILABLE:        return "Available";
        case MULTIBOOT_MEMORY_RESERVED:         return "Reserved";
        case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE: return "ACPI Reclaimable";
        case MULTIBOOT_MEMORY_NVS:              return "ACPI NVS";
        case MULTIBOOT_MEMORY_BADRAM:           return "Bad RAM";
        default:                                return "Unknown";
    }
}

void multiboot2_print_memory_map(u32 mb2_info) {
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

    // Kept exactly as you had it (array-style access), since you said this works fine.
    for (size_t i = 0; i < num_entries; i++) {
        struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
        u64 end_addr = entry->addr + entry->len - 1;
        u64 size_kb  = entry->len / 1024;

        print_padded_hex(entry->addr);
        vga_printf("-");
        print_padded_hex(end_addr);
        vga_printf(" %luKB %s\n", size_kb, multiboot2_memory_type_to_string(entry->type));
    }

    vga_printf("Total entries: %zu\n", num_entries);
    vga_printf("==================\n");
}

// --- Clean API (modules) ---
void multiboot2_get_modules(u32 mb2_info, struct mb2_modules_list* result) {
    struct multiboot_tag *tag;
    if (!result) return;

    result->count = 0;

    for (tag = (struct multiboot_tag*)(uintptr_t)(mb2_info + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((u8*)tag + ((tag->size + 7) & ~7))) {

        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (struct multiboot_tag_module*)tag;

            if (result->count < MB2_MAX_MODULES) {
                result->modules[result->count] = module;
                result->count++;
            }
        }
    }
}

// --- REWORKED: Clean API (memory regions) ---
// Safe iteration by stepping entry_size; fill the caller-provided result.
/*
void multiboot2_get_memory_regions(u32 mb2_info, struct mb2_memory_map* out_map) {
    if (!out_map) return;

    out_map->count = 0;

    struct multiboot_tag_mmap *mmap_tag = multiboot2_get_memory_map(mb2_info);
    if (!mmap_tag) {
        return;
    }

    size_t num_entries = multiboot2_get_memory_map_entries(mmap_tag);
    if (num_entries > MB2_MAX_MEMORY_REGIONS) {
        num_entries = MB2_MAX_MEMORY_REGIONS;
    }
    
    size_t test;
    for (size_t i = 0; i < num_entries; i++) {
        struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
        out_map->regions[i].start_addr = entry->addr;
        out_map->regions[i].size       = entry->len;
        out_map->regions[i].end_addr   = 64; // this ok
        // entry->addr + entry->len; // asigning this crashes
        //entry->addr + entry->len - 1; // asigning this crasheas also
        out_map->regions[i].type       = entry->type;
    }

    out_map->count = num_entries;
}*/


void multiboot2_get_memory_regions(u32 mb2_info, struct mb2_memory_map* out_map) {
    if (!out_map) return;

    out_map->count = 0;

    struct multiboot_tag_mmap *mmap_tag = multiboot2_get_memory_map(mb2_info);
    if (!mmap_tag) {
        return;
    }

    size_t num_entries = multiboot2_get_memory_map_entries(mmap_tag);
    if (num_entries > MB2_MAX_MEMORY_REGIONS) {
        num_entries = MB2_MAX_MEMORY_REGIONS;
    }

    u8 *base = (u8*)&mmap_tag->entries[0];
    size_t esize = mmap_tag->entry_size;

    if (esize < sizeof(struct multiboot_mmap_entry)) {
        esize = sizeof(struct multiboot_mmap_entry);
    }

    for (size_t i = 0; i < num_entries; i++) {
        u8 *src = base + i * mmap_tag->entry_size;

        struct multiboot_mmap_entry entry;
        memcpy(&entry, src, sizeof(struct multiboot_mmap_entry));

        out_map->regions[i].start_addr = entry.addr;
        out_map->regions[i].size       = entry.len;
        out_map->regions[i].type       = entry.type;

        if (entry.len == 0) {
            out_map->regions[i].end_addr = entry.addr;
        } else {
            u64 end = entry.addr + entry.len - 1;
            if (end < entry.addr) {
                out_map->regions[i].end_addr = UINT64_MAX;
            } else {
                out_map->regions[i].end_addr = end;
            }
        }

        //vga_printf("entry at 0x%X - 0x%X %d\n", out_map->regions[i].start_addr, out_map->regions[i].end_addr, out_map->regions[i].type);

        out_map->count++;
    }
}