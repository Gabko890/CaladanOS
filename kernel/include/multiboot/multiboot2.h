#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <cldtypes.h>
#include <stddef.h>

#define MULTIBOOT2_MAGIC 0x36d76289

// Multiboot2 tag types
#define MULTIBOOT_TAG_TYPE_END              0
#define MULTIBOOT_TAG_TYPE_CMDLINE          1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT_TAG_TYPE_MODULE           3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO    4
#define MULTIBOOT_TAG_TYPE_BOOTDEV          5
#define MULTIBOOT_TAG_TYPE_MMAP             6
#define MULTIBOOT_TAG_TYPE_VBE              7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER      8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS     9
#define MULTIBOOT_TAG_TYPE_APM              10
#define MULTIBOOT_TAG_TYPE_EFI32            11
#define MULTIBOOT_TAG_TYPE_EFI64            12
#define MULTIBOOT_TAG_TYPE_SMBIOS           13
#define MULTIBOOT_TAG_TYPE_ACPI_OLD         14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW         15
#define MULTIBOOT_TAG_TYPE_NETWORK          16
#define MULTIBOOT_TAG_TYPE_EFI_MMAP         17
#define MULTIBOOT_TAG_TYPE_EFI_BS           18
#define MULTIBOOT_TAG_TYPE_EFI32_IH         19
#define MULTIBOOT_TAG_TYPE_EFI64_IH         20
#define MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR   21

struct multiboot_tag {
    u32 type;
    u32 size;
} __attribute__((packed));

struct multiboot_tag_string {
    u32 type;
    u32 size;
    char string[0];
} __attribute__((packed));

struct multiboot_tag_module {
    u32 type;
    u32 size;
    u32 mod_start;
    u32 mod_end;
    char cmdline[0];
} __attribute__((packed));

struct multiboot_tag_basic_meminfo {
    u32 type;
    u32 size;
    u32 mem_lower;
    u32 mem_upper;
} __attribute__((packed));

struct multiboot_mmap_entry {
    u64 addr;
    u64 len;
    u32 type;
    u32 zero;
} __attribute__((packed));

struct multiboot_tag_mmap {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    struct multiboot_mmap_entry entries[0];
} __attribute__((packed));

// Memory map types
#define MULTIBOOT_MEMORY_AVAILABLE          1
#define MULTIBOOT_MEMORY_RESERVED           2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE   3
#define MULTIBOOT_MEMORY_NVS                4
#define MULTIBOOT_MEMORY_BADRAM             5

#define MB2_MAX_MODULES 32
#define MB2_MAX_MEMORY_REGIONS 64

// Clean data structures for programmatic access
struct mb2_memory_region {
    u64 start_addr;
    u64 end_addr;
    u64 size;
    u32 type;
};

struct mb2_memory_map {
    size_t count;
    struct mb2_memory_region regions[MB2_MAX_MEMORY_REGIONS];
};

struct mb2_modules_list {
    u8 count;
    struct multiboot_tag_module* modules[MB2_MAX_MODULES];
};

// Function declarations
int  multiboot2_parse(u32 magic, u32 mb2_info);
void multiboot2_print_basic_info(u32 mb2_info);
struct multiboot_tag* multiboot2_find_tag(u32 mb2_info, u32 type);
void multiboot2_print_modules(u32 mb2_info);
void multiboot2_print_memory_map(u32 mb2_info);
struct multiboot_tag_mmap* multiboot2_get_memory_map(u32 mb2_info);
size_t multiboot2_get_memory_map_entries(struct multiboot_tag_mmap* mmap_tag);

void multiboot2_get_modules(u32 mb2_info, struct mb2_modules_list* result);
void multiboot2_get_memory_regions(u32 mb2_info, struct mb2_memory_map* out_map);

#endif // MULTIBOOT2_H

