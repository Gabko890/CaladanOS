#ifndef CLD_SETJMP_H
#define CLD_SETJMP_H

typedef struct { int _stub; } jmp_buf[1];

static inline int setjmp(jmp_buf jb) {
    (void)jb; return 0; // no saved context; always returns 0
}

static inline void longjmp(jmp_buf jb, int val) {
    (void)jb; (void)val;
    // No non-local jump capability; halt to avoid undefined flow
    for(;;) { __asm__ volatile("hlt"); }
}

// POSIX variants if enabled by macros (not used, but declared)
static inline int _setjmp(jmp_buf jb) { return setjmp(jb); }
static inline void _longjmp(jmp_buf jb, int val) { longjmp(jb, val); }

#endif // CLD_SETJMP_H

