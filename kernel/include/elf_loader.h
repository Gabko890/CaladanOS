#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <cldtypes.h>

// ELF header constants
#define EI_NIDENT 16
#define ELF_MAGIC 0x464C457F  // 0x7F + "ELF"

// ELF class
#define ELFCLASS64 2

// ELF data encoding
#define ELFDATA2LSB 1

// ELF version
#define EV_CURRENT 1

// ELF type
#define ET_REL 1    // Relocatable file (.o)

// ELF machine
#define EM_X86_64 62

// Section header types
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8

// Section header flags
#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4

// Symbol binding
#define STB_LOCAL  0
#define STB_GLOBAL 1

// Symbol types
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC   2

// Relocation types for x86_64
#define R_X86_64_64     1   // Direct 64 bit
#define R_X86_64_PC32   2   // PC relative 32 bit signed
#define R_X86_64_32     10  // Direct 32 bit zero extended
#define R_X86_64_32S    11  // Direct 32 bit sign extended

// ELF header structure
typedef struct {
    u8  e_ident[EI_NIDENT];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

// Section header structure
typedef struct {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
} __attribute__((packed)) elf64_shdr_t;

// Symbol table entry
typedef struct {
    u32 st_name;
    u8  st_info;
    u8  st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
} __attribute__((packed)) elf64_sym_t;

// Relocation entry with addend
typedef struct {
    u64 r_offset;
    u64 r_info;
    i64 r_addend;
} __attribute__((packed)) elf64_rela_t;

// Loaded ELF structure
typedef struct {
    void* base_addr;        // Base address where ELF is loaded
    void* exec_base;        // Base address of executable sections
    u64 size;              // Total size allocated
    u64 entry_point;       // Entry point offset from exec_base
    elf64_ehdr_t* header;  // ELF header
    elf64_shdr_t* sections; // Section headers
    char* string_table;     // Section string table
} loaded_elf_t;

// ELF loader functions
int elf_load(const void* elf_data, u64 size, loaded_elf_t* loaded);
void elf_unload(loaded_elf_t* loaded);
int elf_execute(loaded_elf_t* loaded, const char* program_name);

// Executable memory allocation
void* kmalloc_executable(size_t size);
void kfree_executable(void* ptr);

// Helper functions
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define ELF64_R_SYM(i)   ((i) >> 32)
#define ELF64_R_TYPE(i)  ((i) & 0xffffffffL)

#endif // ELF_LOADER_H