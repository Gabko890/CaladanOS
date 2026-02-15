#ifndef CLD_SETJMP_H
#define CLD_SETJMP_H

typedef struct {
    unsigned long long rbx;
    unsigned long long rbp;
    unsigned long long r12;
    unsigned long long r13;
    unsigned long long r14;
    unsigned long long r15;
    unsigned long long rsp;
    unsigned long long rip;
} jmp_buf[1];

__attribute__((always_inline))
static inline int setjmp(jmp_buf jb) {
    int ret;
    __asm__ volatile (
        "movq %%rbx, 0(%1)\n\t"
        "movq %%rbp, 8(%1)\n\t"
        "movq %%r12, 16(%1)\n\t"
        "movq %%r13, 24(%1)\n\t"
        "movq %%r14, 32(%1)\n\t"
        "movq %%r15, 40(%1)\n\t"
        "movq %%rsp, 48(%1)\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "movq %%rax, 56(%1)\n\t"
        "xorl %%eax, %%eax\n\t"
        "1:"
        : "=a"(ret)
        : "r"(jb)
        : "memory"
    );
    return ret;
}

__attribute__((noreturn))
static inline void longjmp(jmp_buf jb, int val) {
    if (val == 0) val = 1;
    __asm__ volatile (
        "movl %0, %%eax\n\t"
        "movq 0(%1), %%rbx\n\t"
        "movq 8(%1), %%rbp\n\t"
        "movq 16(%1), %%r12\n\t"
        "movq 24(%1), %%r13\n\t"
        "movq 32(%1), %%r14\n\t"
        "movq 40(%1), %%r15\n\t"
        "movq 48(%1), %%rsp\n\t"
        "jmp *56(%1)\n\t"
        :
        : "r"(val), "r"(jb)
        : "rax", "memory"
    );
    __builtin_unreachable();
}

// POSIX variants if enabled by macros.
__attribute__((always_inline))
static inline int _setjmp(jmp_buf jb) { return setjmp(jb); }

__attribute__((noreturn))
static inline void _longjmp(jmp_buf jb, int val) { longjmp(jb, val); }

#endif // CLD_SETJMP_H
