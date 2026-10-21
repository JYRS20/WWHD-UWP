// UWP counterpart of the file-opening API used by the embedded interpreter.
#pragma once
#ifdef WWHD_UWP
#include <windows.h>
#include <fileapifromapp.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
static __inline HANDLE wwhd_create_file(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file){
 CREATEFILE2_EXTENDED_PARAMETERS p={0};p.dwSize=sizeof(p);
 p.dwFileAttributes=flags&0xFFFF;p.dwFileFlags=flags&0xFFF00000;p.dwSecurityQosFlags=flags&0x000F0000;
 p.lpSecurityAttributes=security;p.hTemplateFile=template_file;
 return CreateFile2FromAppW(path,access,share,creation,&p);
}
#define CreateFileW wwhd_create_file
static __inline HANDLE wwhd_find_first(LPCWSTR pattern,LPWIN32_FIND_DATAW data){
 return FindFirstFileExFromAppW(pattern,FindExInfoStandard,data,FindExSearchNameMatch,NULL,0);
}
#define FindFirstFileW wwhd_find_first
static __inline int wwhd_wopen(const wchar_t* path,int flags,...){
 DWORD access=(flags&_O_RDWR)?(GENERIC_READ|GENERIC_WRITE):((flags&_O_WRONLY)?GENERIC_WRITE:GENERIC_READ);
 DWORD creation=OPEN_EXISTING;
 if(flags&_O_CREAT)creation=(flags&_O_EXCL)?CREATE_NEW:((flags&_O_TRUNC)?CREATE_ALWAYS:OPEN_ALWAYS);
 else if(flags&_O_TRUNC)creation=TRUNCATE_EXISTING;
 HANDLE handle=wwhd_create_file(path,access,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,creation,FILE_ATTRIBUTE_NORMAL,NULL);
 if(handle==INVALID_HANDLE_VALUE){
  DWORD code=GetLastError();
  errno=(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)?ENOENT:
        ((code==ERROR_FILE_EXISTS||code==ERROR_ALREADY_EXISTS)?EEXIST:
        ((code==ERROR_ACCESS_DENIED||code==ERROR_SHARING_VIOLATION)?EACCES:
        (code==ERROR_DISK_FULL?ENOSPC:EIO)));
  return -1;
 }
 int fd=_open_osfhandle((intptr_t)handle,flags&(_O_APPEND|_O_RDONLY|_O_TEXT|_O_BINARY));
 if(fd<0)CloseHandle(handle);
 return fd;
}
#define _wopen wwhd_wopen
#endif
