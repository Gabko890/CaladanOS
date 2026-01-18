#ifndef CLD_STUB_MATH_H
#define CLD_STUB_MATH_H

// Minimal math header for freestanding build

#ifndef HUGE_VAL
#define HUGE_VAL (1e308)
#endif

double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double pow(double x, double y);
double fabs(double x);

#endif // CLD_STUB_MATH_H

