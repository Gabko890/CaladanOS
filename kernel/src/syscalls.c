#include <syscalls.h>
#include <vgaio.h>
#include <string.h>
#include <process.h>

#include <cldattrs.h>

// Syscall table
static struct syscall_entry syscall_table[MAX_SYSCALLS];
static u32 registered_syscalls = 0;

// Default invalid syscall handler
static long sys_invalid(long arg1, long arg2, long arg3, long arg4, long arg5, long arg6) {
    vga_printf("[SYSCALL] Invalid syscall called with args: %ld, %ld, %ld, %ld, %ld, %ld\n", 
               arg1, arg2, arg3, arg4, arg5, arg6);
    return -1; // EPERM equivalent
}

void syscalls_init(void) {
    // Initialize all syscall entries to invalid
    for (u32 i = 0; i < MAX_SYSCALLS; i++) {
        syscall_table[i].handler = sys_invalid;
        syscall_table[i].name = "invalid";
        syscall_table[i].arg_count = 0;
    }
    
    // Register default syscalls
    register_syscall(SYSCALL_EXIT, sys_exit, "exit", 1);
    register_syscall(SYSCALL_WRITE, sys_write, "write", 3);
    register_syscall(SYSCALL_READ, sys_read, "read", 3);
    register_syscall(SYSCALL_GETPID, sys_getpid, "getpid", 0);
    
    vga_printf("[SYSCALL] System initialized with %u syscalls\n", registered_syscalls);
}

int register_syscall(u32 syscall_num, syscall_handler_t handler, const char* name, u8 arg_count) {
    if (syscall_num >= MAX_SYSCALLS) {
        vga_printf("[SYSCALL] Error: syscall number %u out of range\n", syscall_num);
        return -1;
    }
    
    if (handler == NULL) {
        vga_printf("[SYSCALL] Error: null handler for syscall %u\n", syscall_num);
        return -1;
    }
    
    syscall_table[syscall_num].handler = handler;
    syscall_table[syscall_num].name = name ? name : "unnamed";
    syscall_table[syscall_num].arg_count = arg_count;
    
    if (syscall_table[syscall_num].handler != sys_invalid) {
        registered_syscalls++;
    }
    
    vga_printf("[SYSCALL] Registered syscall %u: %s (%u args)\n", 
               syscall_num, syscall_table[syscall_num].name, arg_count);
    return 0;
}

const struct syscall_entry* get_syscall_info(u32 syscall_num) {
    if (syscall_num >= MAX_SYSCALLS) {
        return NULL;
    }
    return &syscall_table[syscall_num];
}

// Syscall dispatcher - called from assembly interrupt handler
long syscall_dispatch(u32 syscall_num, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6) {
    // Debug: Add minimal debug info for loaded programs
    if (syscall_num == 4) { // write syscall from loaded program
        vga_printf("[SYSCALL-DEBUG] Write syscall from loaded program\n");
    } else if (syscall_num == 1) { // exit syscall
        vga_printf("[SYSCALL-DEBUG] Exit syscall from loaded program\n");
    }
    
    if (syscall_num >= MAX_SYSCALLS) {
        vga_printf("[SYSCALL] Invalid syscall number: %u\n", syscall_num);
        return -1;
    }
    
    const struct syscall_entry* entry = &syscall_table[syscall_num];
    
    // Call the syscall handler
    long result = entry->handler(arg1, arg2, arg3, arg4, arg5, arg6);
    
    if (syscall_num == 4) { // write syscall
        vga_printf("[SYSCALL-DEBUG] Write syscall completed\n");
    } else if (syscall_num == 1) { // exit syscall
        vga_printf("[SYSCALL-DEBUG] Exit syscall completed\n");
    }
    
    return result;
}

// Default syscall implementations
long sys_write(long fd, long buf, long count, long A_UNUSED unused1, long A_UNUSED unused2, long A_UNUSED unused3) {
    // Simple implementation - just write to VGA console for fd 1 (stdout)
    if (fd == 1) {
        const char* str = (const char*)buf;
        for (long i = 0; i < count && str[i]; i++) {
            vga_putchar(str[i]);
        }
        return count;
    }
    return -1; // Invalid fd
}

long sys_read(long fd, long buf, long count, long A_UNUSED unused1, long A_UNUSED unused2, long A_UNUSED unused3) {
    // Stub implementation - not implemented yet
    vga_printf("[SYSCALL] sys_read not implemented (fd=%ld, buf=%p, count=%ld)\n", fd, (void*)buf, count);
    return 0;
}

// Global variable to store exit status from programs
volatile long program_exit_status = 0;
volatile int program_should_exit = 0;

// Simple jump target for sys_exit to return to elf_execute
volatile void* exit_jump_target = NULL;

// Assembly function to perform context switch (we'll implement this)
extern void process_context_switch_exit(u64 exit_status);

long sys_exit(long status, long A_UNUSED unused1, long A_UNUSED unused2, long A_UNUSED unused3, long A_UNUSED unused4, long A_UNUSED unused5) {
    vga_printf("[SYSCALL] sys_exit called with status: %ld\n", status);
    
    process_t* current = process_get_current();
    
    if (current) {
        vga_printf("[SYSCALL] Process %u exit with status: %ld\n", current->pid, status);
        
        // Use the proper process exit mechanism
        process_exit_and_restore_parent(current->pid, status);
        
        vga_printf("[SYSCALL] Process terminated, control should be restored to parent\n");
        
        // This syscall should NEVER return normally to the calling program
        // Instead, we need to jump back to the parent context
        // For now, we'll use a simple longjmp-like mechanism
        
        // Set global flags for the ELF execution loop to detect
        program_should_exit = 1;
        program_exit_status = status;
        
        // The key insight: we need to NOT return to the program that called int 0x80
        // Instead, we need to return control to the parent process (shell)
        
        // We'll implement this by modifying the interrupt return address
        // For now, return normally but set flags for cleanup
        return status;
    } else {
        vga_printf("[SYSCALL] Exit called from kernel context with status: %ld\n", status);
        return status;
    }
}

long sys_getpid(long A_UNUSED unused1, long A_UNUSED unused2, long A_UNUSED unused3, long A_UNUSED unused4, long A_UNUSED unused5, long A_UNUSED unused6) {
    process_t* current = process_get_current();
    if (current) {
        return current->pid;
    } else {
        return 0; // Kernel/shell process
    }
}
