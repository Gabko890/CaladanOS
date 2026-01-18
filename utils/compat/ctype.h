#ifndef CLD_CTYPE_H
#define CLD_CTYPE_H

static inline int isspace(int c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }
static inline int isdigit(int c){ return (c>='0'&&c<='9'); }
static inline int isalpha(int c){ return (c>='A'&&c<='Z')||(c>='a'&&c<='z'); }
static inline int isalnum(int c){ return isalpha(c)||isdigit(c); }
static inline int isxdigit(int c){ return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'); }
static inline int tolower(int c){ return (c>='A'&&c<='Z') ? (c+32) : c; }
static inline int toupper(int c){ return (c>='a'&&c<='z') ? (c-32) : c; }

#endif // CLD_CTYPE_H

