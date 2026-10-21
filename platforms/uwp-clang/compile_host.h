#pragma once
#include <cstddef>
namespace wwhd_compile {
using Progress=bool(*)(void*,unsigned,unsigned); // false cancels between units
using Report=void(*)(void*,const char*);
// Compiles only. No object loading, constructors or guest code execution.
int compile_generated(const wchar_t* headers,const wchar_t* generated,const wchar_t* output,
                      void* context,Report report,Progress progress=nullptr) noexcept;
}
