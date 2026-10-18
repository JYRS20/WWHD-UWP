// SPDX-License-Identifier: MPL-2.0
#pragma once
#ifdef _WIN32
#include <windows.h>
#ifdef WWHD_UWP
#include <fileapifromapp.h>
#endif
namespace host {
// File attributes/flags only; callers do not request security QoS or template handles.
inline HANDLE create_file(const wchar_t* path,DWORD access,DWORD share,DWORD disposition,DWORD attributesAndFlags){
#ifdef WWHD_UWP
 CREATEFILE2_EXTENDED_PARAMETERS parameters{};
 parameters.dwSize=sizeof(parameters);
 parameters.dwFileAttributes=attributesAndFlags&0x0000ffffu;
 parameters.dwFileFlags=attributesAndFlags&0xffff0000u;
 return CreateFile2FromAppW(path,access,share,disposition,&parameters);
#else
 return CreateFileW(path,access,share,nullptr,disposition,attributesAndFlags,nullptr);
#endif
}
}
#endif
