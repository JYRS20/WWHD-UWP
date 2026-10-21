// Runtime-owned image lifetime API. Inputs must be freshly generated/trusted COFF.
#pragma once
#include "clang_api.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WwhdClangObject {const unsigned char* data;size_t size;} WwhdClangObject;
enum {WWHD_LINK_FUNCTION=1,WWHD_LINK_DATA=2};
typedef struct WwhdRuntimeSymbol {const char* name;unsigned long long address;unsigned kind;} WwhdRuntimeSymbol;
typedef struct WwhdLinkRequest {
 unsigned version; // 1
 const WwhdClangObject* objects;size_t object_count;
 const WwhdRuntimeSymbol* symbols;size_t symbol_count;
 size_t arena_bytes; // 64 KiB aligned, 1..512 MiB; reserved, committed on demand.
} WwhdLinkRequest;
typedef struct WwhdLinkedImage WwhdLinkedImage;
WWHD_CLANG_API int wwhd_clang_link_create(const WwhdLinkRequest*,void*,WwhdClangDiagnostic,WwhdLinkedImage**);
WWHD_CLANG_API unsigned long long wwhd_clang_link_find(WwhdLinkedImage*,const char*);
WWHD_CLANG_API int wwhd_clang_link_initialize(WwhdLinkedImage*,void*,WwhdClangDiagnostic);
// Destroy only after all guest threads/callbacks stop. Keep this DLL loaded while an image lives.
WWHD_CLANG_API void wwhd_clang_link_destroy(WwhdLinkedImage*);
#ifdef __cplusplus
}
#endif
