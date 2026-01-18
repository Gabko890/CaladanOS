#ifndef CLD_LOCALE_H
#define CLD_LOCALE_H

#define LC_ALL       0
#define LC_COLLATE   1
#define LC_CTYPE     2
#define LC_MONETARY  3
#define LC_NUMERIC   4
#define LC_TIME      5

struct lconv {
    char *decimal_point;
};

static inline char *setlocale(int category, const char *locale) {
    (void)category; (void)locale; return "C";
}

static inline struct lconv *localeconv(void) {
    static char dp[] = ".";
    static struct lconv lc = { dp };
    return &lc;
}

#endif // CLD_LOCALE_H

