#ifndef SYSCALL_TEST_H
#define SYSCALL_TEST_H

#include <cldtypes.h>

// Test function to demonstrate syscall usage
void test_syscalls(void);

// Inline assembly helper to make syscalls from kernel mode
static inline long syscall0(u32 syscall_num) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n\t"
        "int $0x80"
        : "=a" (ret)
        : "r" ((long)syscall_num)
        : "memory"
    );
    return ret;
}

static inline long syscall1(u32 syscall_num, long arg1) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n\t"
        "mov %2, %%rdi\n\t"
        "int $0x80"
        : "=a" (ret)
        : "r" ((long)syscall_num), "r" (arg1)
        : "memory", "rdi"
    );
    return ret;
}

static inline long syscall3(u32 syscall_num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n\t"
        "mov %2, %%rdi\n\t"
        "mov %3, %%rsi\n\t"
        "mov %4, %%rdx\n\t"
        "int $0x80"
        : "=a" (ret)
        : "r" ((long)syscall_num), "r" (arg1), "r" (arg2), "r" (arg3)
        : "memory", "rdi", "rsi", "rdx"
    );
    return ret;
}

#endif // SYSCALL_TEST_H