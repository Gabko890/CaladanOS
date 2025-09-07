#include <process.h>
#include <vgaio.h>
#include <string.h>
#include <kmalloc.h>

// Process table
static process_t process_table[MAX_PROCESSES];
static u32 next_pid = 1;

// Current running process ID (0 = kernel/shell)
u32 current_pid = 0;

void process_init(void) {
    // Initialize all process entries as unused
    for (u32 i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state = PROCESS_UNUSED;
        process_table[i].pid = 0;
    }
    
    vga_printf("[PROCESS] Process management initialized\n");
}

u32 process_create(const char* name, void* entry_point, void* return_addr, void* elf_base, u64 elf_size) {
    // Find unused process slot
    for (u32 i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_UNUSED) {
            process_t* proc = &process_table[i];
            
            proc->pid = next_pid++;
            proc->state = PROCESS_RUNNING;
            strncpy(proc->name, name ? name : "unknown", PROCESS_NAME_LEN - 1);
            proc->name[PROCESS_NAME_LEN - 1] = '\0';
            proc->entry_point = entry_point;
            proc->return_address = return_addr;
            proc->stack_pointer = NULL; // Will be set when we switch
            proc->exit_status = 0;
            proc->elf_base = elf_base;
            proc->elf_size = elf_size;
            proc->parent_pid = current_pid;  // Set parent to current process
            
            vga_printf("[PROCESS] Created process %u: %s (parent: %u)\n", proc->pid, proc->name, proc->parent_pid);
            return proc->pid;
        }
    }
    
    vga_printf("[PROCESS] Error: No free process slots\n");
    return 0; // No free slot
}

void process_exit(u32 pid, u64 status) {
    process_t* proc = process_get(pid);
    if (!proc) {
        vga_printf("[PROCESS] Error: Process %u not found for exit\n", pid);
        return;
    }
    
    vga_printf("[PROCESS] Process %u (%s) exiting with status %llu\n", 
               proc->pid, proc->name, status);
    
    proc->state = PROCESS_EXITED;
    proc->exit_status = status;
    
    // Clean up ELF memory if allocated
    if (proc->elf_base) {
        // kfree(proc->elf_base);  // COMMENTED FOR NOW TO TEST
        proc->elf_base = NULL;
    }
    
    // Set current process back to kernel/shell
    current_pid = 0;
    
    vga_printf("[PROCESS] Process %u cleaned up, returning to kernel\n", pid);
}

process_t* process_get(u32 pid) {
    for (u32 i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROCESS_UNUSED) {
            return &process_table[i];
        }
    }
    return NULL;
}

process_t* process_get_current(void) {
    if (current_pid == 0) {
        return NULL; // Kernel/shell process
    }
    return process_get(current_pid);
}

void process_set_current(u32 pid) {
    current_pid = pid;
    vga_printf("[PROCESS] Switched to process %u\n", pid);
}

void process_save_context(u32 pid, execution_context_t* context) {
    process_t* proc = process_get(pid);
    if (proc) {
        proc->saved_context = *context;
        vga_printf("[PROCESS] Saved context for process %u\n", pid);
    }
}

void process_restore_context(u32 pid) {
    process_t* proc = process_get(pid);
    if (proc) {
        vga_printf("[PROCESS] Restoring context for process %u\n", pid);
        // This would typically involve assembly code to restore registers
        // For now, we'll implement a simpler approach
    }
}

void process_exit_and_restore_parent(u32 pid, u64 status) {
    process_t* proc = process_get(pid);
    if (!proc) {
        vga_printf("[PROCESS] Error: Process %u not found for exit\n", pid);
        return;
    }
    
    u32 parent_pid = proc->parent_pid;
    vga_printf("[PROCESS] Process %u exiting with status %llu, returning to parent %u\n", 
               pid, status, parent_pid);
    
    // Clean up current process
    process_exit(pid, status);
    
    // Switch back to parent process
    current_pid = parent_pid;
    vga_printf("[PROCESS] Restored control to parent process %u\n", parent_pid);
}