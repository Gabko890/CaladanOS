#include "math.h"

static long long i64_floor(double x) {
    long long i = (long long)x;
    if ((double)i == x) return i;
    return (x >= 0) ? i : (i - 1);
}

double floor(double x) {
    return (double)i64_floor(x);
}

double ceil(double x) {
    long long i = (long long)x;
    if ((double)i == x) return x;
    return (x >= 0) ? (double)(i + 1) : (double)i;
}

double fabs(double x) { return x < 0 ? -x : x; }

double ldexp(double x, int exp) {
    double p = 1.0;
    int e = exp;
    if (e > 0) {
        while (e--) p *= 2.0;
        return x * p;
    } else if (e < 0) {
        while (e++) p *= 0.5;
        return x * p;
    }
    return x;
}

double frexp(double x, int *exp) {
    if (exp) *exp = 0;
    if (x == 0.0) return 0.0;
    int e = 0; double m = x;
    int neg = 0; if (m < 0) { neg = 1; m = -m; }
    while (m >= 1.0) { m *= 0.5; e++; }
    while (m < 0.5) { m *= 2.0; e--; }
    if (exp) *exp = e;
    return neg ? -m : m;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    long long q = (long long)(x / y); // trunc toward zero
    double r = x - (double)q * y;
    // adjust to match fmod sign rules: result has same sign as x
    if (r != 0.0 && ((r < 0) != (x < 0))) r += y;
    return r;
}

double pow(double x, double y) {
    // integer exponents only; otherwise return 0 as a stub
    long long yi = (long long)y;
    if ((double)yi != y) {
        // crude fallback: 1 for x==1, 0 otherwise
        if (x == 1.0) return 1.0;
        return 0.0;
    }
    if (yi == 0) return 1.0;
    int neg = 0;
    if (yi < 0) { neg = 1; yi = -yi; }
    double res = 1.0;
    double base = x;
    while (yi) {
        if (yi & 1) res *= base;
        base *= base;
        yi >>= 1;
    }
    if (neg) {
        if (res == 0.0) return 0.0; // avoid div by zero
        res = 1.0 / res;
    }
    return res;
}
