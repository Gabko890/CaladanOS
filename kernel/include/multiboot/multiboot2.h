#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <stdint.h>
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
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[0];
} __attribute__((packed));

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[0];
} __attribute__((packed));

struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry entries[0];
} __attribute__((packed));

// Memory map types
#define MULTIBOOT_MEMORY_AVAILABLE      1
#define MULTIBOOT_MEMORY_RESERVED       2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE   3
#define MULTIBOOT_MEMORY_NVS            4
#define MULTIBOOT_MEMORY_BADRAM         5

#define MB2_MAX_MODULES 32
#define MB2_MAX_MEMORY_REGIONS 64

// Clean data structures for programmatic access
struct mb2_module_info {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t size;
    const char* name;
};

struct mb2_memory_region {
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t size;
    uint32_t type;
};

struct mb2_modules_list {
    size_t count;
    struct mb2_module_info modules[MB2_MAX_MODULES];
};

struct mb2_memory_map {
    size_t count;
    struct mb2_memory_region regions[MB2_MAX_MEMORY_REGIONS];
};

// Function declarations
int multiboot2_parse(uint32_t magic, uint32_t mb2_info);
void multiboot2_print_basic_info(uint32_t mb2_info);
struct multiboot_tag* multiboot2_find_tag(uint32_t mb2_info, uint32_t type);
void multiboot2_print_modules(uint32_t mb2_info);
void multiboot2_print_memory_map(uint32_t mb2_info);
struct multiboot_tag_mmap* multiboot2_get_memory_map(uint32_t mb2_info);
size_t multiboot2_get_memory_map_entries(struct multiboot_tag_mmap* mmap_tag);

// Clean API functions (no printing, just data)
struct mb2_modules_list multiboot2_get_modules(uint32_t mb2_info);
struct mb2_memory_map multiboot2_get_memory_regions(uint32_t mb2_info);

#endif // MULTIBOOT2_H