#include <elf_loader.h>
#include <kmalloc.h>
#include <string.h>
#include <vgaio.h>
#include <interrupts/interrupts.h>
#include <syscalls.h>
#include <process.h>

// Debug output control
// #define ELF_DEBUG   // Uncomment for detailed ELF loading debug
// #define ELF_EXEC_DEBUG   // Uncomment for detailed execution debug

// Add small delay to prevent timing-related crashes
static inline void small_delay(void) {
    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }
}

static int elf_validate_header(const elf64_ehdr_t* header) {
    // Check ELF magic number
    if (header->e_ident[0] != 0x7F ||
        header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' ||
        header->e_ident[3] != 'F') {
        vga_printf("[ELF] Invalid ELF magic number\n");
        return -1;
    }
    
    // Check 64-bit
    if (header->e_ident[4] != ELFCLASS64) {
        vga_printf("[ELF] Not a 64-bit ELF file\n");
        return -1;
    }
    
    // Check little endian
    if (header->e_ident[5] != ELFDATA2LSB) {
        vga_printf("[ELF] Not little endian\n");
        return -1;
    }
    
    // Check version
    if (header->e_ident[6] != EV_CURRENT) {
        vga_printf("[ELF] Invalid ELF version\n");
        return -1;
    }
    
    // Check file type (relocatable)
    if (header->e_type != ET_REL) {
        vga_printf("[ELF] Not a relocatable file (type=%u)\n", header->e_type);
        return -1;
    }
    
    // Check machine type
    if (header->e_machine != EM_X86_64) {
        vga_printf("[ELF] Not x86_64 (machine=%u)\n", header->e_machine);
        return -1;
    }
    
    return 0;
}

static const char* elf_get_section_name(const loaded_elf_t* loaded, u32 name_offset) {
    if (!loaded->string_table) {
        return "unknown";
    }
    return loaded->string_table + name_offset;
}

static int elf_apply_relocations(loaded_elf_t* loaded, elf64_shdr_t* rela_section) {
    elf64_rela_t* relocations = (elf64_rela_t*)((u8*)loaded->base_addr + rela_section->sh_offset);
    u32 num_relocations = rela_section->sh_size / sizeof(elf64_rela_t);
    
    // Get the target section and symbol table
    elf64_shdr_t* target_section = &loaded->sections[rela_section->sh_info];
    elf64_shdr_t* symtab_section = &loaded->sections[rela_section->sh_link];
    elf64_sym_t* symbols = (elf64_sym_t*)((u8*)loaded->base_addr + symtab_section->sh_offset);
    
    u8* target_data = (u8*)loaded->exec_base + target_section->sh_addr;
    
    vga_printf("[ELF] Applying %u relocations to section %s\n", 
               num_relocations, elf_get_section_name(loaded, target_section->sh_name));
    
    for (u32 i = 0; i < num_relocations; i++) {
        elf64_rela_t* rel = &relocations[i];
        u32 sym_idx = ELF64_R_SYM(rel->r_info);
        u32 type = ELF64_R_TYPE(rel->r_info);
        
        elf64_sym_t* symbol = &symbols[sym_idx];
        u64 symbol_value = symbol->st_value;
        
        // For relocatable files, symbol values are section-relative
        if (symbol->st_shndx != 0 && symbol->st_shndx < loaded->header->e_shnum) {
            elf64_shdr_t* sym_section = &loaded->sections[symbol->st_shndx];
            symbol_value += (u64)loaded->exec_base + sym_section->sh_addr;
        }
        
        u8* patch_location = target_data + rel->r_offset;
        
        switch (type) {
            case R_X86_64_64: {
                // Direct 64-bit address
                u64 value = symbol_value + rel->r_addend;
                *(u64*)patch_location = value;
                break;
            }
            case R_X86_64_PC32: {
                // PC-relative 32-bit
                u64 patch_addr = (u64)patch_location;
                i64 value = (i64)(symbol_value + rel->r_addend - patch_addr);
                if (value < -0x80000000LL || value > 0x7FFFFFFFLL) {
                    vga_printf("[ELF] PC32 relocation out of range\n");
                    return -1;
                }
                *(i32*)patch_location = (i32)value;
                break;
            }
            case R_X86_64_32:
            case R_X86_64_32S: {
                // Direct 32-bit
                u64 value = symbol_value + rel->r_addend;
                if (type == R_X86_64_32S) {
                    if ((i64)value < -0x80000000LL || (i64)value > 0x7FFFFFFFLL) {
                        vga_printf("[ELF] 32S relocation out of range\n");
                        return -1;
                    }
                } else {
                    if (value > 0xFFFFFFFFULL) {
                        vga_printf("[ELF] 32 relocation out of range\n");
                        return -1;
                    }
                }
                *(u32*)patch_location = (u32)value;
                break;
            }
            default:
                vga_printf("[ELF] Unsupported relocation type: %u\n", type);
                return -1;
        }
    }
    
    return 0;
}

int elf_load(const void* elf_data, u64 size, loaded_elf_t* loaded) {
    if (!elf_data || !loaded || size < sizeof(elf64_ehdr_t)) {
        vga_printf("[ELF] Invalid parameters\n");
        return -1;
    }
    
    const elf64_ehdr_t* header = (const elf64_ehdr_t*)elf_data;
    
    // Validate ELF header
    if (elf_validate_header(header) != 0) {
        return -1;
    }
    
#ifdef ELF_DEBUG
    vga_printf("[ELF] Loading relocatable ELF file (%u sections)\n", header->e_shnum);
#endif
    
    // Calculate total size needed for all allocated sections
    const elf64_shdr_t* sections = (const elf64_shdr_t*)((const u8*)elf_data + header->e_shoff);
    u64 total_size = 0;
    u64 max_align = 1;
    
    for (u16 i = 0; i < header->e_shnum; i++) {
        if (sections[i].sh_flags & SHF_ALLOC) {
            if (sections[i].sh_addralign > max_align) {
                max_align = sections[i].sh_addralign;
            }
            u64 section_end = sections[i].sh_size;
            if (section_end > total_size) {
                total_size = section_end;
            }
        }
    }
    
    // Add space for the ELF data itself
    total_size += size + max_align;
    
    // Allocate executable memory
    void* base = kmalloc_executable(total_size);
    if (!base) {
        vga_printf("[ELF] Failed to allocate %llu bytes executable memory\n", total_size);
        return -1;
    }
    
#ifdef ELF_DEBUG
    vga_printf("[ELF] Allocated %llu bytes at 0x%llx\n", total_size, (u64)base);
#endif
    
    // Copy the entire ELF data
    memcpy(base, elf_data, size);
    
    // Initialize loaded structure
    loaded->base_addr = base;
    loaded->size = total_size;
    loaded->header = (elf64_ehdr_t*)base;
    loaded->sections = (elf64_shdr_t*)((u8*)base + header->e_shoff);
    loaded->entry_point = 0;
    
    // Find string table
    if (header->e_shstrndx != 0 && header->e_shstrndx < header->e_shnum) {
        elf64_shdr_t* strtab_hdr = &loaded->sections[header->e_shstrndx];
        loaded->string_table = (char*)base + strtab_hdr->sh_offset;
    } else {
        loaded->string_table = NULL;
    }
    
    // Allocate space for sections and update their addresses
    u8* section_base = (u8*)base + size;
    section_base = (u8*)(((u64)section_base + max_align - 1) & ~(max_align - 1));
    
    // Store the executable base address
    loaded->exec_base = section_base;
    
    u64 current_offset = 0;
    for (u16 i = 0; i < header->e_shnum; i++) {
        elf64_shdr_t* section = &loaded->sections[i];
        
        if (section->sh_flags & SHF_ALLOC) {
            // Align section
            if (section->sh_addralign > 1) {
                current_offset = (current_offset + section->sh_addralign - 1) & ~(section->sh_addralign - 1);
            }
            
            // Set section address
            section->sh_addr = current_offset;
            
#ifdef ELF_DEBUG
            vga_printf("[ELF] Section %s: offset=0x%llx size=%llu flags=0x%llx\n",
                       elf_get_section_name(loaded, section->sh_name),
                       section->sh_addr, section->sh_size, section->sh_flags);
#endif
            
            // Copy section data if it has data
            if (section->sh_type == SHT_PROGBITS && section->sh_size > 0) {
                memcpy(section_base + section->sh_addr, 
                       (u8*)base + section->sh_offset, 
                       section->sh_size);
            } else if (section->sh_type == SHT_NOBITS && section->sh_size > 0) {
                // Zero out BSS sections
                memset(section_base + section->sh_addr, 0, section->sh_size);
            }
            
            current_offset += section->sh_size;
        }
    }
    
    // Apply relocations
    for (u16 i = 0; i < header->e_shnum; i++) {
        if (loaded->sections[i].sh_type == SHT_RELA) {
            if (elf_apply_relocations(loaded, &loaded->sections[i]) != 0) {
                elf_unload(loaded);
                return -1;
            }
        }
    }
    
    // Find entry point (look for _start or main function)
    for (u16 i = 0; i < header->e_shnum; i++) {
        if (loaded->sections[i].sh_type == SHT_SYMTAB) {
            elf64_sym_t* symbols = (elf64_sym_t*)((u8*)base + loaded->sections[i].sh_offset);
            u32 num_symbols = loaded->sections[i].sh_size / sizeof(elf64_sym_t);
            elf64_shdr_t* str_section = &loaded->sections[loaded->sections[i].sh_link];
            char* str_table = (char*)base + str_section->sh_offset;
            
            for (u32 j = 0; j < num_symbols; j++) {
                const char* sym_name = str_table + symbols[j].st_name;
                if (strcmp(sym_name, "_start") == 0 || strcmp(sym_name, "main") == 0) {
                    elf64_shdr_t* sym_section = &loaded->sections[symbols[j].st_shndx];
                    loaded->entry_point = sym_section->sh_addr + symbols[j].st_value;
                    vga_printf("[ELF] Found entry point: %s at offset 0x%llx\n", sym_name, loaded->entry_point);
                    break;
                }
            }
        }
    }
    
    vga_printf("[ELF] ELF file loaded successfully\n");
    return 0;
}

void elf_unload(loaded_elf_t* loaded) {
    if (loaded && loaded->base_addr) {
        kfree_executable(loaded->base_addr);
        loaded->base_addr = NULL;
        loaded->exec_base = NULL;
        loaded->size = 0;
        loaded->entry_point = 0;
        loaded->header = NULL;
        loaded->sections = NULL;
        loaded->string_table = NULL;
    }
}

int elf_execute(loaded_elf_t* loaded, const char* program_name) {
    if (!loaded || !loaded->base_addr) {
        vga_printf("[ELF] Invalid loaded ELF or no base address\n");
        return -1;
    }
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] Executing at offset 0x%llx\n", loaded->entry_point);
#endif
    
    // Calculate absolute address using exec_base
    void* entry_addr = (u8*)loaded->exec_base + loaded->entry_point;
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] base_addr=0x%llx exec_base=0x%llx entry_point=0x%llx\n", 
               (u64)loaded->base_addr, (u64)loaded->exec_base, loaded->entry_point);
    vga_printf("[ELF] Calling entry point at 0x%llx\n", (u64)entry_addr);
#endif
    
    // Save the current execution context (this is the shell/parent context)
    // In a real implementation, we'd save all CPU registers here
    // For now, we'll just save basic info
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] Saving parent context (PID %u)\n", current_pid);
#endif
    
    // Create a process for this program
    u32 pid = process_create(program_name, entry_addr, NULL, loaded->base_addr, loaded->size);
    if (pid == 0) {
        vga_printf("[ELF] Failed to create process\n");
        return -1;
    }
    
    // Switch to the new process
    process_set_current(pid);
    
    // Enable interrupts for syscalls
    interrupts_enable();
    
    // Clear exit flags
    program_should_exit = 0;
    program_exit_status = 0;
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] About to execute program with PID %u...\n", pid);
#endif
    
    // Execute the program
    typedef int (*elf_func_t)(void);
    elf_func_t elf_function = (elf_func_t)entry_addr;
    
    int result = 0;
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] Starting program execution...\n");
#endif
    
    // Call the program function normally
    result = elf_function();
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] Program %u completed with result: %d\n", pid, result);
#endif
    
    // Check if program called sys_exit during execution
    if (program_should_exit) {
        result = (int)program_exit_status;
        vga_printf("[ELF] Program %u exited via sys_exit with status: %d\n", pid, result);
        small_delay();  // Prevent timing-related crash
        // Process was already cleaned up by sys_exit
        // Memory was already freed, so mark it as NULL to prevent double-free
        loaded->base_addr = NULL;
        small_delay();  // Prevent timing-related crash
        loaded->exec_base = NULL;
        small_delay();  // Prevent timing-related crash
    } else {
        vga_printf("[ELF] Program %u returned normally with status: %d\n", pid, result);
        process_exit_and_restore_parent(pid, result);
        // Memory was freed by process_exit, mark as NULL
        loaded->base_addr = NULL;
        loaded->exec_base = NULL;
    }
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] Cleaning up program %u\n", pid);
#endif
    
    // Clear exit flags for next program
    program_should_exit = 0;
    program_exit_status = 0;
    
    // Return control to kernel/shell
    current_pid = 0;
    
#ifdef ELF_EXEC_DEBUG
    vga_printf("[ELF] *** ABOUT TO RETURN TO SHELL - result=%d ***\n", result);
    vga_printf("[ELF] *** CURRENT PID IS NOW: %u ***\n", current_pid);
#endif
    
    return result;
}

// Executable memory allocator
// For now, just use regular kmalloc since heap pages should be executable
// TODO: Implement proper executable heap if needed
void* kmalloc_executable(size_t size) {
    return kmalloc(size);
}

void kfree_executable(void* ptr) {
    kfree(ptr);
}
