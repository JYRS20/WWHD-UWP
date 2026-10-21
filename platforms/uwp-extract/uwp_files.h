#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#ifdef WWHD_EXTRACT_UWP
#include <windows.h>
#include <fileapifromapp.h>
#include <limits>
namespace wwhd_extract {
class InputFile {
 HANDLE handle_=INVALID_HANDLE_VALUE;
 std::streamsize count_=0; bool good_=false;
public:
 InputFile(const std::filesystem::path& p,std::ios::openmode) {
  handle_=CreateFile2FromAppW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,nullptr);
  good_=handle_!=INVALID_HANDLE_VALUE;
 }
 ~InputFile(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
 InputFile(const InputFile&)=delete;InputFile& operator=(const InputFile&)=delete;
 explicit operator bool()const{return good_;}
 void clear(){good_=handle_!=INVALID_HANDLE_VALUE;}
 std::streamsize gcount()const{return count_;}
 void read(char* dst,std::streamsize length){
  count_=0;if(!good_||length<0){good_=false;return;}
  while(count_<length){DWORD got=0;DWORD wanted=DWORD((std::min)(length-count_,std::streamsize(1<<20)));
   if(!ReadFile(handle_,dst+count_,wanted,&got,nullptr)){good_=false;return;}
   count_+=got;if(got!=wanted){good_=false;return;}
  }
 }
 void seekg(std::streamoff offset,std::ios::seekdir direction=std::ios::beg){
  LARGE_INTEGER pos{};pos.QuadPart=offset;
  good_=SetFilePointerEx(handle_,pos,nullptr,direction==std::ios::end?FILE_END:(direction==std::ios::cur?FILE_CURRENT:FILE_BEGIN))!=FALSE;
 }
 std::streamoff tellg(){LARGE_INTEGER zero{},pos{};if(!SetFilePointerEx(handle_,zero,&pos,FILE_CURRENT)){good_=false;return -1;}return pos.QuadPart;}
};
inline bool make_directories(const std::filesystem::path& path){
 if(path.empty()||path==path.root_path())return true;
 if(CreateDirectoryFromAppW(path.c_str(),nullptr))return true;
 DWORD e=GetLastError();if(e==ERROR_ALREADY_EXISTS){WIN32_FILE_ATTRIBUTE_DATA a{};return GetFileAttributesExFromAppW(path.c_str(),GetFileExInfoStandard,&a)&&(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);}
 if(e!=ERROR_PATH_NOT_FOUND||!make_directories(path.parent_path()))return false;
 return CreateDirectoryFromAppW(path.c_str(),nullptr)!=FALSE||GetLastError()==ERROR_ALREADY_EXISTS;
}
class OutputFile {
 HANDLE handle_=INVALID_HANDLE_VALUE;bool good_=false;
public:
 explicit OutputFile(const std::filesystem::path& path){handle_=CreateFile2FromAppW(path.c_str(),GENERIC_WRITE,0,CREATE_ALWAYS,nullptr);good_=handle_!=INVALID_HANDLE_VALUE;}
 ~OutputFile(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
 explicit operator bool()const{return good_;}
 void write(const uint8_t* bytes,uint64_t count){while(good_&&count){DWORD n=DWORD((std::min)(count,uint64_t(1<<20))),done=0;good_=WriteFile(handle_,bytes,n,&done,nullptr)&&done==n;bytes+=done;count-=done;}}
 bool close(){if(handle_!=INVALID_HANDLE_VALUE){good_=CloseHandle(handle_)&&good_;handle_=INVALID_HANDLE_VALUE;}return good_;}
};
}
#else
namespace wwhd_extract {
using InputFile=std::ifstream;
inline bool make_directories(const std::filesystem::path& path){std::error_code ec;std::filesystem::create_directories(path,ec);return !ec;}
class OutputFile {
 std::ofstream out_;
public:
 explicit OutputFile(const std::filesystem::path& p):out_(p,std::ios::binary|std::ios::trunc){}
 explicit operator bool()const{return bool(out_);}
 void write(const uint8_t* p,uint64_t n){out_.write(reinterpret_cast<const char*>(p),std::streamsize(n));}
 bool close(){out_.close();return bool(out_);}
};
}
#endif
