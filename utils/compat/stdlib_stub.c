#include "stdlib.h"

static int is_space(char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }
static int is_digit(char c){ return c>='0' && c<='9'; }

double strtod(const char *nptr, char **endptr) {
    const char *s = nptr; if (!s) { if (endptr) *endptr = (char*)nptr; return 0.0; }
    while (is_space(*s)) s++;
    int neg = 0; if (*s=='+'||*s=='-') { if (*s=='-') neg=1; s++; }
    long long intpart = 0; int any=0;
    while (is_digit(*s)) { any=1; intpart = intpart*10 + (*s - '0'); s++; }
    double val = (double)intpart;
    if (*s == '.') {
        s++; double frac=0.0, base=1.0; while (is_digit(*s)) { frac = frac*10.0 + (*s - '0'); base *= 10.0; s++; any=1; }
        val += frac / base;
    }
    if ((*s=='e'||*s=='E')) {
        s++; int esign=1; if (*s=='+'||*s=='-') { if (*s=='-') esign=-1; s++; }
        int exp=0; while (is_digit(*s)) { exp = exp*10 + (*s - '0'); s++; }
        // crude power of 10
        double p = 1.0; double ten = 10.0; int e = exp; while (e) { if (e & 1) p *= ten; ten *= 10.0; e >>= 1; }
        if (esign < 0) { if (p != 0.0) val /= p; }
        else val *= p;
    }
    if (!any) { if (endptr) *endptr = (char*)nptr; return 0.0; }
    if (endptr) *endptr = (char*)s;
    return neg ? -val : val;
}

long double strtold(const char *nptr, char **endptr) {
    double d = strtod(nptr, endptr);
    return (long double)d;
}

void abort(void) {
    // Halt the CPU to indicate fatal error path
    for(;;) { __asm__ volatile("hlt"); }
}
