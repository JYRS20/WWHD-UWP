#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <cerrno>
#ifdef WWHD_UWP
#include "file_access.h"
#endif
// Local compatibility names used by the HLE filesystem only. Enumeration uses real
// host directories; guest paths and guest handles remain owned by hle/fs.cpp.
#define stat _stat64
#define fstat _fstat64
#define fileno _fileno
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode)&_S_IFMT)==_S_IFDIR)
#endif
struct dirent { char d_name[4096]; };
#ifdef WWHD_UWP
struct DIR {HANDLE search=INVALID_HANDLE_VALUE;WIN32_FIND_DATAW found{};dirent entry{};bool first=true;};
inline DIR* opendir(const char* path){
 auto native=std::filesystem::u8path(path);WIN32_FILE_ATTRIBUTE_DATA attributes{};
 if(!GetFileAttributesExFromAppW(native.c_str(),GetFileExInfoStandard,&attributes)||!(attributes.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return nullptr;
 auto directory=new DIR;
 directory->search=FindFirstFileExFromAppW((native/L"*").c_str(),FindExInfoStandard,&directory->found,FindExSearchNameMatch,nullptr,0);
 if(directory->search==INVALID_HANDLE_VALUE){auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_NO_MORE_FILES){delete directory;return nullptr;}}
 return directory;
}
inline dirent* readdir(DIR* directory){
 if(directory->search==INVALID_HANDLE_VALUE)return nullptr;
 if(!directory->first&&!FindNextFileW(directory->search,&directory->found))return nullptr;
 directory->first=false;
 int size=WideCharToMultiByte(CP_UTF8,0,directory->found.cFileName,-1,directory->entry.d_name,sizeof directory->entry.d_name,nullptr,nullptr);
 return size>0?&directory->entry:nullptr;
}
inline int closedir(DIR* directory){if(directory->search!=INVALID_HANDLE_VALUE)FindClose(directory->search);delete directory;return 0;}
#else
struct DIR {
 std::filesystem::directory_iterator cursor,end;
 dirent entry{};
 bool first=true;
};
inline DIR* opendir(const char* path) {
 std::error_code ec;
 auto cursor=std::filesystem::directory_iterator(path,ec);
 if(ec)return nullptr;
 return new DIR{std::move(cursor)};
}
inline dirent* readdir(DIR* directory) {
 std::error_code ec;
 if(directory->cursor==directory->end)return nullptr;
 if(!directory->first)directory->cursor.increment(ec);
 directory->first=false;
 if(ec||directory->cursor==directory->end)return nullptr;
 auto name=directory->cursor->path().filename().string();
 if(name.size()>=sizeof directory->entry.d_name)return nullptr;
 memcpy(directory->entry.d_name,name.c_str(),name.size()+1);
 return &directory->entry;
}
inline int closedir(DIR* directory) {delete directory;return 0;}
#endif

#else
#include <dirent.h>
#endif
namespace host {
inline int file_stat(const char* path,struct stat* out){
#ifdef WWHD_UWP
 WIN32_FILE_ATTRIBUTE_DATA data{};
 if(!GetFileAttributesExFromAppW(std::filesystem::u8path(path).c_str(),GetFileExInfoStandard,&data))return -1;
 std::memset(out,0,sizeof *out);
 out->st_mode=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY?_S_IFDIR:_S_IFREG)|_S_IREAD;
 if(!(data.dwFileAttributes&FILE_ATTRIBUTE_READONLY))out->st_mode|=_S_IWRITE;
 out->st_size=(uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow;out->st_nlink=1;
 auto seconds=[](FILETIME t){uint64_t n=(uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;return n>=116444736000000000ull?__time64_t((n-116444736000000000ull)/10000000):__time64_t(0);};
 out->st_atime=seconds(data.ftLastAccessTime);out->st_mtime=seconds(data.ftLastWriteTime);out->st_ctime=seconds(data.ftCreationTime);
 return 0;
#else
 return stat(path,out);
#endif
}
inline FILE* open_file(const char* path,const char* mode){
#ifdef WWHD_UWP
 const bool plus=std::strchr(mode,'+')!=nullptr,append=mode[0]=='a',write=mode[0]=='w'||append;
 DWORD access=plus?GENERIC_READ|GENERIC_WRITE:(write?GENERIC_WRITE:GENERIC_READ);
 DWORD creation=mode[0]=='w'?CREATE_ALWAYS:(append?OPEN_ALWAYS:OPEN_EXISTING);
 auto handle=create_file(std::filesystem::u8path(path).c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE,creation,FILE_ATTRIBUTE_NORMAL);
 if(handle==INVALID_HANDLE_VALUE){errno=GetLastError()==ERROR_ACCESS_DENIED?EACCES:ENOENT;return nullptr;}
 int flags=plus?_O_RDWR:(write?_O_WRONLY:_O_RDONLY);
 flags|=std::strchr(mode,'b')?_O_BINARY:_O_TEXT;if(append)flags|=_O_APPEND;
 int fd=_open_osfhandle(reinterpret_cast<intptr_t>(handle),flags);
 if(fd<0){CloseHandle(handle);return nullptr;}
 FILE* file=_fdopen(fd,mode);if(!file)_close(fd);return file;
#else
 return std::fopen(path,mode);
#endif
}

inline int64_t file_tell(FILE* file) {
#ifdef _WIN32
 return _ftelli64(file);
#else
 return ftello(file);
#endif
}
inline int file_seek(FILE* file,uint64_t offset,int origin) {
#ifdef _WIN32
 return _fseeki64(file,(int64_t)offset,origin);
#else
 return fseeko(file,(off_t)offset,origin);
#endif
}
}
