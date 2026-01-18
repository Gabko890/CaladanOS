#ifndef CLD_STDLIB_H
#define CLD_STDLIB_H

#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

double strtod(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
void abort(void);

static inline int abs(int x) { return x < 0 ? -x : x; }
static inline long labs(long x) { return x < 0 ? -x : x; }

static inline int atoi(const char *s) {
    if (!s) return 0; int sign = 1; long v = 0;
    if (*s=='+'||*s=='-') { if (*s=='-') sign=-1; s++; }
    while (*s>='0' && *s<='9') { v = v*10 + (*s - '0'); s++; }
    return (int)(sign * v);
}

static inline long atol(const char *s) { return (long)atoi(s); }

#endif // CLD_STDLIB_H
