// USB compilation host for the in-process compiler; never embeds generated game code.
#include "compile_host.h"
#include "clang_api.h"
#include "../uwp-extract/uwp_files.h"
#include <winrt/Windows.System.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace fs=std::filesystem;
using Files=std::map<std::string,std::string>;
std::string read(const fs::path& path,size_t limit=128u<<20){
 wwhd_extract::InputFile file(path,std::ios::binary);
 if(!file)throw std::runtime_error("Cannot open compiler input");
 file.seekg(0,std::ios::end);auto size=file.tellg();
 if(size<0||uint64_t(size)>limit)throw std::runtime_error("Compiler input exceeds limit");
 file.seekg(0);std::string data(size_t(size),'\0');file.read(data.data(),size);
 if(file.gcount()!=size)throw std::runtime_error("Compiler input truncated or USB unavailable");
 return data;
}
struct Result {std::vector<unsigned char> object;std::string diagnostic;bool overflow=false;};
void object_out(void* p,const unsigned char* bytes,size_t size){
 auto& r=*static_cast<Result*>(p);
 if(size>(128u<<20)-r.object.size()){r.overflow=true;return;}
 try{r.object.insert(r.object.end(),bytes,bytes+size);}catch(...){r.overflow=true;}
}
void diagnostic_out(void* p,const char* bytes,size_t size){
 auto& r=*static_cast<Result*>(p);
 try{r.diagnostic.append(bytes,(std::min)(size,size_t(16384)-r.diagnostic.size()));}catch(...){r.overflow=true;}
}
void write(const fs::path& path,const unsigned char* bytes,size_t size){
 wwhd_extract::OutputFile file(path);
 if(!file)throw std::runtime_error("Cannot create compiled output");
 file.write(bytes,size);if(!file.close())throw std::runtime_error("Cannot write compiled output (USB unavailable or disk full)");
}
}
int wwhd_compile::compile_generated(const wchar_t* headers,const wchar_t* generated,const wchar_t* output,
 void* context,Report report,Progress progress) noexcept {
 try{
  if(!headers||!generated||!output||!report)throw std::runtime_error("Missing compiler host arguments");
  auto emit=[&](const std::string& s){report(context,s.c_str());};
  Files common;
  // Minimal project-authored C declarations plus the actual runtime ABI headers.
  const char* names[]={"stdint.h","string.h","math.h","stdio.h","stdlib.h","ppc.h","guest_addr.h","recomp_table.h","wwhd_guest_abi.h","wwhd_mod.h","verify_tap.h","verify_tap_gen.h"};
  for(const char* name:names)common[name]=read(fs::path(headers)/name);
  common["funcs.h"]=read(fs::path(generated)/"funcs.h");
  std::vector<std::string> units;
  for(int i=0;i<78;i++){char name[32];std::snprintf(name,sizeof name,"code_%03d.c",i);units.emplace_back(name);}
  units.emplace_back("table.c");units.emplace_back("imports.c");
  if(!wwhd_extract::make_directories(output))throw std::runtime_error("Cannot create object directory");
  auto start=std::chrono::steady_clock::now();uint64_t total=0,peak_usage=0;
  std::string rows;
  emit("COMPILE begin: 80 units, x64 COFF, Clang 20.1.8, O2; sequential compilation");
  for(size_t index=0;index<units.size();index++){
   if(progress&&!progress(context,unsigned(index),unsigned(units.size())))throw std::runtime_error("Preparation cancelled; incomplete outputs will not be used");
   const auto& unit=units[index];auto source=read(fs::path(generated)/unit);
   emit("COMPILING "+std::to_string(index+1)+"/80 "+unit+" source_bytes="+std::to_string(source.size()));
   std::vector<WwhdClangFile> inputs;inputs.reserve(common.size()+1);
   for(const auto& [name,data]:common)inputs.push_back({name.c_str(),data.data(),data.size()});
   inputs.push_back({unit.c_str(),source.data(),source.size()});
   WwhdClangRequest request{1,unit.c_str(),inputs.data(),inputs.size()};Result result;
   auto unit_start=std::chrono::steady_clock::now();
   int status=wwhd_clang_compile_files(&request,&result,object_out,diagnostic_out);
   if(!result.diagnostic.empty())emit(result.diagnostic);
   if(status||result.overflow)throw std::runtime_error("Clang failed on "+unit+" status="+std::to_string(status));
   if(result.object.size()<20||result.object[0]!=0x64||result.object[1]!=0x86)throw std::runtime_error("Invalid x64 COFF compiler output");
   auto name=fs::path(unit).replace_extension(".obj");write(fs::path(output)/name,result.object.data(),result.object.size());
   auto size=result.object.size();total+=size;
   auto usage=winrt::Windows::System::MemoryManager::AppMemoryUsage();peak_usage=(std::max)(peak_usage,usage);
   auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-unit_start).count();
   emit("COMPILED "+unit+" object_bytes="+std::to_string(size)+" seconds="+std::to_string(seconds)+" app_memory_mib="+std::to_string(usage>>20));
   if(index)rows+=",";
   rows+="\n    {\"file\":\""+unit+"\",\"object_bytes\":"+std::to_string(size)+",\"seconds\":"+std::to_string(seconds)+"}";
  }
  if(progress&&!progress(context,80,80))throw std::runtime_error("Preparation cancelled; incomplete outputs will not be used");
  auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  std::string summary="{\"version\":1,\"compiler\":\"Clang 20.1.8\",\"target\":\"x86_64-pc-windows-msvc\",\"optimization\":\"O2\",\"units\":80,\"object_bytes\":"+std::to_string(total)+",\"seconds\":"+std::to_string(elapsed)+",\"observed_memory_after_units_bytes\":"+std::to_string(peak_usage)+",\"scope\":\"compilation only; no game link or execution\",\"files\":["+rows+"\n]}\n";
  write(fs::path(output)/"compilation-complete.json",reinterpret_cast<const unsigned char*>(summary.data()),summary.size());
  emit("PASS all 80 generated C units compiled and saved on USB; object_bytes="+std::to_string(total)+" seconds="+std::to_string(elapsed));
  return 0;
 }catch(const std::exception& e){if(report)report(context,e.what());return -1;}
 catch(...){if(report)report(context,"Unknown compilation host failure");return -1;}
}
