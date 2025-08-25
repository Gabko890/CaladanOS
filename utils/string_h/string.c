#include "string.h"

/* strlen */
size_t strlen(const char *s) {
    if (!s) return 0;
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t maxlen) {
    if (!s) return 0;
    size_t i = 0;
    for (; i < maxlen && s[i]; ++i);
    return i;
}

/* strcmp / strncmp */
int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) { ++s1; ++s2; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (n && *s1 && (*s1 == *s2)) { ++s1; ++s2; --n; }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

/* strcpy / strncpy (safe truncate) */
char *strcpy(char *dst, const char *src) {
    if (!dst || !src) return NULL;
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    if (!dst || !src) return NULL;
    size_t i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    if (i < n) dst[i] = '\0';
    return dst;
}

/* strcat / strncat (safe append) */
char *strcat(char *dst, const char *src) {
    if (!dst || !src) return NULL;
    char *d = dst;
    while (*d) ++d;
    while ((*d++ = *src++));
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    if (!dst || !src) return NULL;
    char *d = dst;
    while (*d) ++d;
    size_t i = 0;
    while (i < n && src[i]) { *d++ = src[i++]; }
    *d = '\0';
    return dst;
}

/* strchr / strrchr / strstr */
char *strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        ++s;
    }
    return (c == 0) ? (char*)s : NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        ++s;
    }
    return (c == 0) ? (char*)s : (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && (*h == *n)) { ++h; ++n; }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

/* memcpy / memmove / memset / memcmp */
void *memcpy(void *dst, const void *src, size_t n) {
    if (!dst || !src) return NULL;
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    if (!dst || !src) return NULL;
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) d[i-1] = s[i-1];
    }
    return dst;
}

void *memset(void *s, int c, size_t n) {
    if (!s) return NULL;
    unsigned char *p = s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    const unsigned char *a = s1, *b = s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

