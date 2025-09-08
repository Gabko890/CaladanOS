#ifndef CLDATTRS_H

#define A_PACKED       __attribute__((packed))
#define A_ALIGNED(x)   __attribute__((aligned(x)))
#define A_NORETURN     __attribute__((noreturn))
#define A_UNUSED       __attribute__((unused))
#define A_INLINE       inline __attribute__((always_inline))
#define A_WEAK         __attribute__((weak))
#define A_LIKELY(x)    __builtin_expect(!!(x), 1)
#define A_UNLIKELY(x)  __builtin_expect(!!(x), 0)

#endif // CLDATTRS_H
