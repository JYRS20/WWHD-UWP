#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*WwhdPythonReport)(void*,const char*,size_t);
// Synchronous, one interpreter run per process. All paths belong to the host.
int wwhd_python_run(const wchar_t* home,const wchar_t* tools,const wchar_t* work,const wchar_t* rpx,const char* script,void* context,WwhdPythonReport report);
#ifdef __cplusplus
}
#endif
