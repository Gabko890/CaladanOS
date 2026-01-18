#include "stdio.h"

#include <stdarg.h>
#include <stddef.h>

static int itoa10(long long v, char *buf, size_t max) {
    char tmp[32]; int i=0; int neg=0; if (v<0){ neg=1; v=-v; }
    if (v==0){ if (max>0) buf[0]='0'; return 1 + neg; }
    while (v && i<(int)sizeof(tmp)) { tmp[i++] = (char)('0' + (v%10)); v/=10; }
    size_t pos=0; if (neg && pos<max) buf[pos++]='-';
    while (i>0 && pos<max) buf[pos++] = tmp[--i];
    return (int)pos;
}

static int utoa10(unsigned long long v, char *buf, size_t max) {
    char tmp[32]; int i=0; if (v==0){ if (max>0) buf[0]='0'; return 1; }
    while (v && i<(int)sizeof(tmp)) { tmp[i++] = (char)('0' + (v%10)); v/=10; }
    size_t pos=0; while (i>0 && pos<max) buf[pos++] = tmp[--i];
    return (int)pos;
}

static int dtoa6(double d, char *buf, size_t max) {
    // crude: int part + '.' + 6 decimals, strip trailing zeros
    if (max==0) return 0;
    long long ip = (long long)d;
    double frac = d - (double)ip;
    if (frac < 0) frac = -frac;
    int pos = itoa10(ip, buf, max);
    if ((size_t)pos < max) buf[pos++] = '.'; else return pos;
    unsigned long long f = (unsigned long long)(frac * 1000000.0);
    char tmp[16]; int fi = 0;
    for (int k=0;k<6;k++){ tmp[fi++] = (char)('0' + (f/100000u)); f = (f%100000u)*10u; }
    // write and trim
    for (int k=0;k<fi && (size_t)pos<max;k++) buf[pos++] = tmp[k];
    while (pos>0 && buf[pos-1]=='0') pos--;
    if (pos>0 && buf[pos-1]=='.') buf[pos++]='0';
    return pos;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            if (pos + 1 < size) str[pos] = *p;
            pos++;
            continue;
        }
        p++;
        // handle length modifiers (only 'l' and 'll')
        int longcnt = 0;
        while (*p == 'l') { longcnt++; p++; }
        char spec = *p;
        if (spec == '%') {
            if (pos + 1 < size) str[pos] = '%'; pos++;
        } else if (spec == 's') {
            const char *s = va_arg(ap, const char*); if (!s) s = "";
            while (*s) { if (pos + 1 < size) str[pos] = *s; pos++; s++; }
        } else if (spec == 'd' || spec == 'i') {
            long long v = (longcnt>=2) ? va_arg(ap, long long) : (longcnt==1 ? va_arg(ap, long) : va_arg(ap, int));
            char tmp[64]; int n = itoa10(v, tmp, sizeof(tmp));
            for (int i=0;i<n;i++){ if (pos + 1 < size) str[pos] = tmp[i]; pos++; }
        } else if (spec == 'u') {
            unsigned long long v = (longcnt>=2) ? va_arg(ap, unsigned long long) : (longcnt==1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            char tmp[64]; int n = utoa10(v, tmp, sizeof(tmp));
            for (int i=0;i<n;i++){ if (pos + 1 < size) str[pos] = tmp[i]; pos++; }
        } else if (spec == 'g' || spec == 'f') {
            double d = va_arg(ap, double);
            char tmp[64]; int n = dtoa6(d, tmp, sizeof(tmp));
            for (int i=0;i<n;i++){ if (pos + 1 < size) str[pos] = tmp[i]; pos++; }
        } else {
            // unsupported specifier: skip
        }
    }
    if (size > 0) {
        str[(pos < size) ? pos : (size - 1)] = '\0';
    }
    return (int)pos;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return n;
}

