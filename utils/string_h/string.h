#ifndef FSTRING_H
#define FSTRING_H

#include <stddef.h>

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);

char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
int strcoll(const char *s1, const char *s2);

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* FSTRING_H */
