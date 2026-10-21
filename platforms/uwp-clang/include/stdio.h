/* Opaque Windows CRT stream declarations for guest_addr.h diagnostics. */
#pragma once
typedef struct WwhdCompilerFile FILE;
FILE* __acrt_iob_func(unsigned);
#define stderr (__acrt_iob_func(2))
int fprintf(FILE*,const char*,...);
