/* Minimal declarations used by the translator and ppc.h. */
#pragma once
#define isnan(x) __builtin_isnan(x)
double sqrt(double);
double fabs(double);
double fma(double,double,double);
double nearbyint(double);
double trunc(double);
double ceil(double);
double floor(double);
