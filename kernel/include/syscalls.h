#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <cldtypes.h>

// Maximum number of syscalls supported
#define MAX_SYSCALLS 256

// Syscall numbers - start with common ones
#define SYSCALL_EXIT       1
#define SYSCALL_WRITE      4
#define SYSCALL_READ       3
#define SYSCALL_OPEN       5
#define SYSCALL_CLOSE      6
#define SYSCALL_GETPID     20

// Syscall handler function pointer type
// Takes 6 arguments (rdi, rsi, rdx, rcx, r8, r9) and returns long
typedef long (*syscall_handler_t)(long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);

// Syscall registration structure
struct syscall_entry {
    syscall_handler_t handler;
    const char* name;           // For debugging/logging
    u8 arg_count;              // Number of arguments expected
};

// Initialize the syscall system
void syscalls_init(void);

// Register a new syscall
int register_syscall(u32 syscall_num, syscall_handler_t handler, const char* name, u8 arg_count);

// Get syscall info for debugging
const struct syscall_entry* get_syscall_info(u32 syscall_num);

// Default syscall handlers for demonstration
long sys_write(long fd, long buf, long count, long unused1, long unused2, long unused3);
long sys_read(long fd, long buf, long count, long unused1, long unused2, long unused3);
long sys_exit(long status, long unused1, long unused2, long unused3, long unused4, long unused5);
long sys_getpid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6);

// Program exit handling for ELF programs
extern volatile long program_exit_status;
extern volatile int program_should_exit;

#endif // SYSCALLS_H