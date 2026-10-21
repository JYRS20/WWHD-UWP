/* Minimal declarations used by ppc.h; resolved by the runtime linker. */
#pragma once
typedef unsigned long long size_t;
void* memcpy(void* __restrict,const void* __restrict,size_t);
void* memset(void*,int,size_t);
void* memmove(void*,const void*,size_t);
