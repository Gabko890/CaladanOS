#ifndef PROCESS_H
#define PROCESS_H

#include <cldtypes.h>

#define MAX_PROCESSES 32
#define PROCESS_NAME_LEN 64

// Process states
typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_RUNNING,
    PROCESS_EXITED
} process_state_t;

// Execution context for process switching
typedef struct {
    u64 rax, rbx, rcx, rdx;
    u64 rsi, rdi, rbp, rsp;
    u64 r8, r9, r10, r11;
    u64 r12, r13, r14, r15;
    u64 rip, rflags;
} execution_context_t;

// Process control block
typedef struct {
    u32 pid;
    process_state_t state;
    char name[PROCESS_NAME_LEN];
    void* entry_point;              // Program entry point
    void* return_address;           // Where to return control after exit
    void* stack_pointer;            // Saved stack pointer of caller
    u64 exit_status;               // Exit status when process exits
    void* elf_base;                // Base address for cleanup
    u64 elf_size;                  // Size for cleanup
    u32 parent_pid;                // Parent process PID
    execution_context_t saved_context; // Saved context for parent restoration
} process_t;

// Process management functions
void process_init(void);
u32 process_create(const char* name, void* entry_point, void* return_addr, void* elf_base, u64 elf_size);
void process_exit(u32 pid, u64 status);
process_t* process_get(u32 pid);
process_t* process_get_current(void);
void process_set_current(u32 pid);

// Context switching functions
void process_save_context(u32 pid, execution_context_t* context);
void process_restore_context(u32 pid);
void process_exit_and_restore_parent(u32 pid, u64 status);

// Global current process ID
extern u32 current_pid;

#endif // PROCESS_H