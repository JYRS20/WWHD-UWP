#include "extract_api.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
std::string data(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void report(void*,const char* s){std::cout<<s<<"\n";}
bool cancel(void*,uint64_t,uint64_t){return false;}
int main(int argc,char** argv){
 if(argc!=2)return 2;fs::path root=argv[1];int failures=0;
 auto call=[&](const char* image,const char* disc,const char* common,const char* output,wwhd_extract::Progress progress=nullptr){auto a=(root/image).string(),b=(root/disc).string(),c=(root/common).string(),d=(root/output).string();return wwhd_extract::extract_disc(a.c_str(),b.c_str(),c.c_str(),d.c_str(),nullptr,progress,report);};
 auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";if(!ok)failures++;};
 check(call("disc.wux","disc_wux.key","common_raw.bin","embedded-wux")==0,"WUX extraction");
 for(auto& entry:fs::recursive_directory_iterator(root/"out_wux"))if(entry.is_regular_file())check(data(entry.path())==data(root/"embedded-wux"/fs::relative(entry.path(),root/"out_wux")),"extracted fixture matches original");
 check(call("disc.wud","disc.key","common_hex.txt","embedded-wud")==0,"WUD extraction");
 check(call("disc.wux","bad_disc.key","common_raw.bin","wrong-disc")==4,"wrong disc key rejected");
 check(call("disc.wux","disc_wux.key","bad_common.txt","wrong-common")==6,"wrong common key rejected");
 check(call("disc.wux","malformed.txt","common_raw.bin","malformed")==3,"malformed disc key rejected");
 check(call("damaged.wud","damaged.key","common_raw.bin","damaged")==8,"damaged hashed block rejected");
 check(call("disc.wux","disc_wux.key","common_raw.bin","cancelled",cancel)==11,"cancellation");
 check(!fs::exists(root/"cancelled"),"cancel before output creation");
 return failures?1:0;
}
