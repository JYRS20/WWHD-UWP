// Versioned, in-memory input API. No SDK headers or game code are embedded here.
#pragma once
#include <stddef.h>
#ifdef WWHD_CLANG_EXPORTS
#define WWHD_CLANG_API __declspec(dllexport)
#else
#define WWHD_CLANG_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*WwhdClangOutput)(void*,const unsigned char*,size_t);
typedef void (*WwhdClangDiagnostic)(void*,const char*,size_t);
typedef struct WwhdClangFile {
    const char* path; // Relative virtual path, e.g. ppc.h or code_000.c.
    const char* data;
    size_t size;
} WwhdClangFile;
typedef struct WwhdClangRequest {
    unsigned version; // Must be 1.
    const char* main_file;
    const WwhdClangFile* files;
    size_t file_count;
} WwhdClangRequest;
WWHD_CLANG_API int wwhd_clang_compile_files(const WwhdClangRequest*,void*,WwhdClangOutput,WwhdClangDiagnostic);
WWHD_CLANG_API int wwhd_clang_compile(const char*,void*,WwhdClangOutput,WwhdClangDiagnostic);
#ifdef __cplusplus
}
#endif
