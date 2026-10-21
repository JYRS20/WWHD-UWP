// Load the user's compiled USB objects into the runtime. No game instructions are embedded here.
#ifdef WWHD_DYNAMIC_GAMECODE
#include "dynamic_game.h"
#include "recomp_table.h"
#include "guest_addr.h"
#include "../../platforms/uwp-extract/uwp_files.h"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <bcrypt.h>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

extern "C" {
const RecompEntry* g_recomp_funcs=nullptr;unsigned g_recomp_func_count=0;
const RecompImport* g_recomp_imports=nullptr;unsigned g_recomp_import_count=0;
uint32_t g_recomp_entry_point=0;
const GuestStep* g_guest_code_steps=nullptr;unsigned g_guest_code_step_count=0;
const GuestStep* g_guest_data_steps=nullptr;unsigned g_guest_data_step_count=0;
uint32_t g_guest_code_lo=0,g_guest_code_hi=0,g_guest_data_lo=0,g_guest_data_hi=0;
const GuestChanged* g_guest_changed_code=nullptr;unsigned g_guest_changed_code_count=0;
const char* g_guest_build_name="unbound";const char* g_guest_build_title_id="";
void ppc_mod_register(unsigned,uint8_t*,const PpcFunc*);
void hle_unimplemented(Cpu*,const char*,const char*);
void ppc_cr_poisoned(Cpu*,int,uint32_t);
}
namespace {
namespace fs=std::filesystem;
// Intentionally pinned until process exit: the game has detached guest/render callbacks.
// Never unload its DLL, objects or unwind tables while those callbacks can exist.
WwhdLinkedImage* pinned_image=nullptr;
int float_marker=0;
std::string read(const fs::path& path,size_t limit=64u<<20){
 wwhd_extract::InputFile file(path,std::ios::binary);
 if(!file)throw std::runtime_error("Cannot open dynamic game input");
 file.seekg(0,std::ios::end);auto size=file.tellg();
 if(size<0||uint64_t(size)>limit)throw std::runtime_error("Dynamic game input exceeds limit");
 file.seekg(0);std::string value(size_t(size),'\0');file.read(value.data(),size);
 if(file.gcount()!=size)throw std::runtime_error("Dynamic game input is incomplete or USB unavailable");
 return value;
}
std::string sha256(const std::string& bytes){
 unsigned char digest[32]{};
 if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),digest,32)<0)
  throw std::runtime_error("Cannot hash dynamic game input");
 static constexpr char hex[]="0123456789abcdef";std::string text;
 for(auto value:digest){text+=hex[value>>4];text+=hex[value&15];}return text;
}
void linker_report(void*,const char* text,size_t size){LOG("[dynamic link] %.*s",int(size),text);}
uint64_t address(WwhdLinkedImage* image,const char* name){auto value=wwhd_clang_link_find(image,name);if(!value)throw std::runtime_error(std::string("Missing loaded symbol ")+name);return value;}
template<class T> const T* pointer(WwhdLinkedImage* image,const char* name){return reinterpret_cast<const T*>(address(image,name));}
unsigned count(WwhdLinkedImage* image,const char* name){return *pointer<unsigned>(image,name);}
struct NativeRegistry {
 std::deque<std::string> names;std::vector<WwhdRuntimeSymbol> symbols;
 void hle(const char* lib,const char* name,PpcFunc function){names.emplace_back(std::string("imp_")+lib+"_"+name);symbols.push_back({names.back().c_str(),reinterpret_cast<uint64_t>(function),WWHD_LINK_FUNCTION});}
};
void bind_tables(WwhdLinkedImage* image){
 g_recomp_func_count=count(image,"g_recomp_func_count");g_recomp_import_count=count(image,"g_recomp_import_count");
 g_recomp_entry_point=count(image,"g_recomp_entry_point");
 if(g_recomp_func_count!=39713||g_recomp_import_count!=408||!g_recomp_entry_point)throw std::runtime_error("Unexpected dynamic USA game metadata");
 g_recomp_funcs=pointer<RecompEntry>(image,"g_recomp_funcs");g_recomp_imports=pointer<RecompImport>(image,"g_recomp_imports");
 bool entry_found=false;uint32_t last=0;
 for(unsigned i=0;i<g_recomp_func_count;i++){
  const auto& item=g_recomp_funcs[i];
  if(item.addr<=last||!item.fn)throw std::runtime_error("Invalid dynamic function table");
  last=item.addr;entry_found|=item.addr==g_recomp_entry_point;
 }
 if(!entry_found)throw std::runtime_error("Dynamic guest entry missing from function table");
 g_guest_code_steps=pointer<GuestStep>(image,"g_guest_code_steps");g_guest_code_step_count=count(image,"g_guest_code_step_count");
 g_guest_data_steps=pointer<GuestStep>(image,"g_guest_data_steps");g_guest_data_step_count=count(image,"g_guest_data_step_count");
 g_guest_code_lo=count(image,"g_guest_code_lo");g_guest_code_hi=count(image,"g_guest_code_hi");
 g_guest_data_lo=count(image,"g_guest_data_lo");g_guest_data_hi=count(image,"g_guest_data_hi");
 g_guest_changed_code=pointer<GuestChanged>(image,"g_guest_changed_code");g_guest_changed_code_count=count(image,"g_guest_changed_code_count");
 g_guest_build_name=pointer<char>(image,"g_guest_build_name");g_guest_build_title_id=pointer<char>(image,"g_guest_build_title_id");
 if(std::strcmp(g_guest_build_name,"USA")||std::strcmp(g_guest_build_title_id,"0005000010143500"))throw std::runtime_error("Unsupported dynamic game region");
}
}
void dynamic_game_load(const std::wstring& objects,const std::wstring& rpx){
 if(pinned_image)throw std::runtime_error("Dynamic game already bound; restart this application");
 if(sha256(read(rpx))!="c4f0ab300542e0bfc462696850534e71db2ad02288a7eb55e5a4cd4062f16153")throw std::runtime_error("RPX differs from the supported USA v0 copy");
 auto home=winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path();
 std::istringstream profile(read(fs::path(home.c_str())/L"dynamic-object-profile.txt",16384));
 std::map<std::string,std::string> expected;std::string digest,name;
 while(profile>>digest>>name){if(digest.size()!=64||!expected.emplace(name,digest).second)throw std::runtime_error("Invalid packaged object profile");}
 if(expected.size()!=80)throw std::runtime_error("Incomplete packaged object profile");
 std::vector<std::string> inputs;inputs.reserve(80);
 for(unsigned i=0;i<80;i++){
  char unit[32];if(i<78)std::snprintf(unit,sizeof unit,"code_%03u.obj",i);else std::snprintf(unit,sizeof unit,"%s",i==78?"table.obj":"imports.obj");
  auto bytes=read(fs::path(objects)/unit);auto found=expected.find(unit);
  if(found==expected.end()||sha256(bytes)!=found->second)throw std::runtime_error(std::string("USB object differs from the verified compiler profile: ")+unit);
  inputs.push_back(std::move(bytes));
 }
 LOG("[dynamic] PASS RPX and all 80 object checksums; no game code linked into the app");
 NativeRegistry registry;dynamic_game_native_hooks(registry.symbols);
 hle_visit([](void* context,const char* lib,const char* name,PpcFunc function){static_cast<NativeRegistry*>(context)->hle(lib,name,function);},&registry);
#define NATIVE(name) registry.symbols.push_back({#name,reinterpret_cast<uint64_t>(&name),WWHD_LINK_FUNCTION})
 NATIVE(hle_unimplemented);NATIVE(ppc_dispatch);NATIVE(ppc_fres);NATIVE(ppc_frsqrte);NATIVE(ppc_host_call);
 NATIVE(ppc_mod_register);NATIVE(ppc_mod_run);NATIVE(ppc_preempt);NATIVE(ppc_trace_enter);NATIVE(ppc_trap);
 NATIVE(ppc_unimplemented);NATIVE(ppc_timebase);NATIVE(ppc_cr_poisoned);
#undef NATIVE
 registry.symbols.push_back({"fma",reinterpret_cast<uint64_t>(static_cast<double(*)(double,double,double)>(&::fma)),WWHD_LINK_FUNCTION});
 registry.symbols.push_back({"_fltused",reinterpret_cast<uint64_t>(&float_marker),WWHD_LINK_DATA});
 registry.symbols.push_back({"g_ppc_trace",reinterpret_cast<uint64_t>(&g_ppc_trace),WWHD_LINK_DATA});
 registry.symbols.push_back({"g_core_preempt",reinterpret_cast<uint64_t>(&g_core_preempt[0]),WWHD_LINK_DATA});
 registry.symbols.push_back({"g_mod_hook_flags",reinterpret_cast<uint64_t>(&g_mod_hook_flags),WWHD_LINK_DATA});
 std::vector<WwhdClangObject> buffers;buffers.reserve(inputs.size());
 for(const auto& bytes:inputs)buffers.push_back({reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size()});
 WwhdLinkRequest request{1,buffers.data(),buffers.size(),registry.symbols.data(),registry.symbols.size(),512u<<20};
 WwhdLinkedImage* image=nullptr;
 int status=wwhd_clang_link_create(&request,nullptr,linker_report,&image);
 if(status||!image)throw std::runtime_error("Dynamic game COFF link failed; inspect console log");
 try{
  bind_tables(image);dynamic_game_bind_forwarders(image);
  if(wwhd_clang_link_initialize(image,nullptr,linker_report))throw std::runtime_error("Dynamic game constructor initialization failed");
  pinned_image=image;
  LOG("[dynamic] PASS real runtime binding: %u guest functions, %u imports; entry %08X",g_recomp_func_count,g_recomp_import_count,g_recomp_entry_point);
 }catch(...){wwhd_clang_link_destroy(image);throw;}
}
#endif
