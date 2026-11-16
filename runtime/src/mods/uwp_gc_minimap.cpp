// SPDX-License-Identifier: MPL-2.0
#ifdef WWHD_BUILTIN_GC_MINIMAP
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "uwp_gc_minimap.h"
#include "platform/host.h"
#include "platform/file_access.h"
#include "platform/uwp_browser.h"
#include "imgui.h"
#include "../../../platforms/uwp-python/python_host.h"
#include <zstd.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <vector>

namespace uwp_gc_minimap {
namespace {
namespace fs=std::filesystem;
using mods::json::Value;
constexpr size_t kReadLimit=32u<<20,kWriteLimit=1u<<20;
struct Config {int renderer=0;bool hd_fade=true,hd_background=true,hd_wind=true,hd_fit_islands=true;float hd_max=1,hd_min=.7f,hd_background_opacity=.4f,hd_size=160;bool enabled=false,dungeons=true;int style=0;float x=6,y=18,size=225,opacity=.8f;std::string data;};
Config config;
bool hd_only=false;
std::mutex config_mutex,status_mutex;
std::future<void> worker;
std::atomic<bool> busy=false,cancelled=false;
std::atomic<unsigned> written=0;
std::string message;
fs::path package_path,source_path,output_path;
bool source_is_disc=false;
std::string utf8(const fs::path& p){auto u=p.u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
fs::path storage(){return fs::u8path(host::config_dir());}
std::string read(const fs::path& p,size_t cap){
 std::ifstream in(p,std::ios::binary);if(!in)return {};
 in.seekg(0,std::ios::end);auto n=in.tellg();if(n<0||uint64_t(n)>cap)return {};
 in.seekg(0);std::string text(size_t(n),'\0');in.read(text.data(),n);return in?text:std::string();
}
bool ready(const std::string& path){
 try{auto data=fs::u8path(path);auto m=mods::json::parse(read(data/"maps-ready.json",128u<<10));
  return m.get("ready").type==Value::Bool&&m.get("ready").boolean&&fs::is_regular_file(data/"dungeons-ready.bin");
 }catch(...){return false;}
}
void save(){
 // Only small per-user preferences; maps and game sources never enter the APPX.
 Value v;v["renderer"]=config.renderer?"hd":"gamecube";v["hd_size"]=double(config.hd_size);v["hd_fit_islands"]=config.hd_fit_islands;v["hd_fade"]=config.hd_fade;v["hd_background"]=config.hd_background;v["hd_wind"]=config.hd_wind;v["hd_max"]=double(config.hd_max);v["hd_min"]=double(config.hd_min);v["hd_background_opacity"]=double(config.hd_background_opacity);v["enabled"]=config.enabled;v["dungeons"]=config.dungeons;v["style"]=config.style?"original":"vector";
 v["x"]=double(config.x);v["y"]=double(config.y);v["size"]=double(config.size);v["opacity"]=double(config.opacity);v["data"]=config.data;
 fs::create_directories(storage());std::ofstream out(storage()/"gc-minimap.json",std::ios::binary);
 out<<mods::json::dump(v)<<'\n';if(!out)throw std::runtime_error("Cannot save minimap preferences.");
}
void note(const std::string& text){std::lock_guard lock(status_mutex);message=text;}
void report(void*,const char* text,size_t n){
 std::ofstream out(storage()/"wwhd-minimap.log",std::ios::app|std::ios::binary);out.write(text,std::streamsize(n));
}
void check_cancel(){if(cancelled.load())throw std::runtime_error("Map preparation cancelled; previous maps were preserved.");}
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
PyObject* bytes(const fs::path& path,uint64_t offset,size_t count){
 check_cancel();if(count>kReadLimit)throw std::runtime_error("Map source read exceeds 32 MiB.");
 Handle file{host::create_file(path.c_str(),GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT)};
 BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{},pos{};pos.QuadPart=LONGLONG(offset);
 if(file.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.value,&info)||
    (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||!GetFileSizeEx(file.value,&size)||
    offset>uint64_t(size.QuadPart)||count>uint64_t(size.QuadPart)-offset||!SetFilePointerEx(file.value,pos,nullptr,FILE_BEGIN))
  throw std::runtime_error("Cannot read the GameCube source. Check the selected disc/folder and USB connection.");
 PyObject* result=PyBytes_FromStringAndSize(nullptr,Py_ssize_t(count));if(!result)return nullptr;
 DWORD done=0;if(!ReadFile(file.value,PyBytes_AS_STRING(result),DWORD(count),&done,nullptr)||done!=count){Py_DECREF(result);throw std::runtime_error("GameCube source is truncated or disconnected.");}
 return result;
}
template<class F>PyObject* boundary(F&& f){try{return f();}catch(const std::exception& e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject* is_disc(PyObject*,PyObject*){return PyBool_FromLong(source_is_disc);}
PyObject* read_game(PyObject*,PyObject* args){return boundary([&]()->PyObject*{
 unsigned long long offset;Py_ssize_t n;if(!PyArg_ParseTuple(args,"Kn",&offset,&n))return nullptr;
 if(!source_is_disc||n<0)throw std::runtime_error("Invalid disc read.");return bytes(source_path,offset,size_t(n));});}
bool relative_ok(const std::string& name){
 if(name.empty()||name.size()>240||name.find_first_of("\\:")!=std::string::npos||name.front()=='/'||name.back()=='/')return false;
 auto p=fs::u8path(name);if(p.is_absolute())return false;for(const auto& part:p)if(part.empty()||part==L"."||part==L"..")return false;return true;
}
PyObject* read_input(PyObject*,PyObject* args){return boundary([&]()->PyObject*{
 const char* name;if(!PyArg_ParseTuple(args,"s",&name))return nullptr;
 if(source_is_disc||!relative_ok(name))throw std::runtime_error("Invalid extracted game path.");
 auto path=source_path/fs::u8path(name),check=source_path;
 for(auto const& part:fs::u8path(name)){check/=part;WIN32_FILE_ATTRIBUTE_DATA a{};if(GetFileAttributesExFromAppW(check.c_str(),GetFileExInfoStandard,&a)&&(a.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked map inputs are unsupported.");}
 Handle file{host::create_file(path.c_str(),GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT)};
 if(file.value==INVALID_HANDLE_VALUE){PyErr_SetString(PyExc_FileNotFoundError,"GameCube room file was not found.");return nullptr;}
 LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)||size.QuadPart<0||size.QuadPart>kReadLimit)throw std::runtime_error("Room archive exceeds the read limit.");
 return bytes(path,0,size_t(size.QuadPart));});}
PyObject* write_data(PyObject*,PyObject* args){return boundary([&]()->PyObject*{
 const char* name;Py_buffer input{};if(!PyArg_ParseTuple(args,"sy*",&name,&input))return nullptr;
 struct Release{Py_buffer* p;~Release(){PyBuffer_Release(p);}} release{&input};check_cancel();
 std::string leaf=name;if(!relative_ok(leaf)||leaf.find('/')!=std::string::npos||leaf[0]=='.'||leaf.size()>128||input.len<0||size_t(input.len)>kWriteLimit)
  throw std::runtime_error("Invalid map output.");
 auto path=output_path/fs::u8path(leaf);Handle file{host::create_file(path.c_str(),GENERIC_WRITE,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL)};
 DWORD done=0;if(file.value==INVALID_HANDLE_VALUE||!WriteFile(file.value,input.buf,DWORD(input.len),&done,nullptr)||done!=input.len)
  throw std::runtime_error("Cannot write prepared maps. Check free storage space.");
 ++written;Py_RETURN_NONE;});}
PyObject* decompress(PyObject*,PyObject* args){return boundary([&]()->PyObject*{
 Py_buffer input{};Py_ssize_t cap;if(!PyArg_ParseTuple(args,"y*n",&input,&cap))return nullptr;
 struct Release{Py_buffer* p;~Release(){PyBuffer_Release(p);}} release{&input};check_cancel();
 if(input.len<=0||size_t(input.len)>kReadLimit||cap<0||size_t(cap)>kReadLimit+1)throw std::runtime_error("RVZ decompression exceeds limits.");
 const size_t frame=ZSTD_findFrameCompressedSize(input.buf,size_t(input.len));
 if(ZSTD_isError(frame)||frame!=size_t(input.len))throw std::runtime_error("Invalid or trailing RVZ Zstandard frame.");
 auto* context=ZSTD_createDCtx();if(!context)return PyErr_NoMemory();
 struct Free{ZSTD_DCtx* p;~Free(){ZSTD_freeDCtx(p);}} free{context};
 auto limit=ZSTD_DCtx_setParameter(context,ZSTD_d_windowLogMax,25);if(ZSTD_isError(limit))throw std::runtime_error("Cannot set RVZ window limit.");
 std::vector<char> out(size_t(cap),0);size_t n=ZSTD_decompressDCtx(context,out.data(),out.size(),input.buf,size_t(input.len));
 if(ZSTD_isError(n))throw std::runtime_error("Invalid, oversized or truncated RVZ Zstandard data.");
 return PyBytes_FromStringAndSize(out.data(),Py_ssize_t(n));});}
PyMethodDef methods[]={{"is_disc",is_disc,METH_NOARGS,nullptr},{"read_game",read_game,METH_VARARGS,nullptr},
 {"read_input",read_input,METH_VARARGS,nullptr},{"write_data",write_data,METH_VARARGS,nullptr},
 {"decompress",decompress,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyModuleDef definition={PyModuleDef_HEAD_INIT,"wwhd_minimap_native",nullptr,-1,methods};
PyObject* initialize(){return PyModule_Create(&definition);}
}

void configure(){
 package_path=fs::path(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str())/L"gc-minimap";
 const auto policy=read(package_path.parent_path()/"minimap-mode.txt",32);
 hd_only=policy=="hd-only"||policy=="hd-only\n"||policy=="hd-only\r\n";
 try{auto v=mods::json::parse(read(storage()/"gc-minimap.json",16384));
  config.renderer=v.get("renderer").string()=="hd";
  config.hd_fit_islands=v.get("hd_fit_islands").type!=Value::Bool||v.get("hd_fit_islands").boolean;
  config.hd_fade=v.get("hd_fade").type!=Value::Bool||v.get("hd_fade").boolean;config.hd_background=v.get("hd_background").type!=Value::Bool||v.get("hd_background").boolean;config.hd_wind=v.get("hd_wind").type!=Value::Bool||v.get("hd_wind").boolean;
  config.enabled=v.get("enabled").type==Value::Bool&&v.get("enabled").boolean;
  config.dungeons=v.get("dungeons").type!=Value::Bool||v.get("dungeons").boolean;config.style=v.get("style").string()=="original";
  config.data=v.get("data").string();auto number=[&](const char* key,float fallback,float lo,float hi){auto& a=v.get(key);return a.type==Value::Number&&a.number>=lo&&a.number<=hi?float(a.number):fallback;};
  config.x=number("x",6,0,1200);config.y=number("y",18,0,650);config.size=number("size",225,64,600);config.opacity=number("opacity",.8f,0,1);
  config.hd_size=number("hd_size",160,120,240);
  config.hd_max=number("hd_max",1,0,1);config.hd_min=number("hd_min",.7f,0,config.hd_max);config.hd_background_opacity=number("hd_background_opacity",.4f,0,1);
 }catch(...){config=Config{};}
#ifdef WWHD_BUILTIN_HD_MINIMAP
 // The Xbox trial exposes only HD. Migrate old GC preferences once; an HD
 // user's explicit disabled setting still remains disabled on later starts.
 if(hd_only&&config.renderer!=1){config.renderer=1;config.enabled=true;}
#endif
#ifndef WWHD_BUILTIN_HD_MINIMAP
 config.renderer=0;
#endif
#ifdef WWHD_BUILTIN_HD_MINIMAP
 if(config.enabled&&config.renderer==1)_putenv_s("WWHD_UWP_HD_MINIMAP","1");
#endif
 if(config.enabled&&config.renderer==0&&ready(config.data)){
  _putenv_s("WWHD_UWP_GC_MINIMAP_DATA",config.data.c_str());_putenv_s("WWHD_UWP_GC_MINIMAP_PACKAGE",utf8(package_path).c_str());
 }
 // Explicit isolated research request, never part of package defaults.
 if(const char* source=std::getenv("WWHD_TEST_MINIMAP_PREPARE");source&&*source){begin_preparation(source);worker.get();}
}
Value options(){std::lock_guard lock(config_mutex);Value v;v["style"]=config.style?"original":"vector";v["dungeons"]=config.dungeons;v["x"]=double(config.x);v["y"]=double(config.y);v["size"]=double(config.size);v["opacity"]=double(config.opacity);return v;}
Value hd_options(){std::lock_guard lock(config_mutex);Value v;v["size"]=double(config.hd_size);v["fit_islands"]=config.hd_fit_islands;v["fade"]=config.hd_fade;v["background"]=config.hd_background;v["wind"]=config.hd_wind;v["maximum"]=double(config.hd_max);v["minimum"]=double(config.hd_min);v["background_opacity"]=double(config.hd_background_opacity);return v;}
void begin_preparation(const std::string& source){
 if(busy.exchange(true))return;
 cancelled=false;written=0;note("Preparing local maps. Keep the source connected; this can take several minutes.");
 worker=std::async(std::launch::async,[source]{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  try{
   source_path=fs::u8path(source);WIN32_FILE_ATTRIBUTE_DATA a{};
   if(!GetFileAttributesExFromAppW(source_path.c_str(),GetFileExInfoStandard,&a)||(a.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("GameCube source is unavailable. Select it again using the USB browser.");
   source_is_disc=!(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
   auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
   output_path=storage()/"MinimapData"/("prepared-"+std::to_string(stamp));fs::create_directories(output_path);
   std::ofstream(storage()/"wwhd-minimap.log",std::ios::trunc)<<"GameCube minimap preparation 0.3.0; native UWP I/O\n";
   if(PyImport_AppendInittab("wwhd_minimap_native",initialize)!=0)throw std::runtime_error("Cannot register map preparation services.");
   auto tools=package_path/"tools",home=package_path.parent_path();
   std::string script="import json, prepare, wwhd_minimap_native as context\n";
   if(std::getenv("WWHD_TEST_MINIMAP_PREPARE"))script+=
    "for name, fn in [('negative read', lambda: context.read_game(0,-1)), ('oversized read', lambda: context.read_game(0,33554433)), ('path traversal', lambda: context.write_data('../escape',b'bad')), ('invalid zstd', lambda: context.decompress(b'bad',1024)), ('oversized zstd', lambda: context.decompress(b'bad',33554434))]:\n"
    " try:\n  fn()\n except (ValueError, OverflowError):\n  print('MINIMAP BOUNDARY PASS',name)\n else:\n  raise AssertionError(name)\n";
   script+="result=prepare.prepare(context)\nprint('MINIMAP PREPARE PASS', json.dumps(result,sort_keys=True))\n";
   if(wwhd_python_run(home.c_str(),tools.c_str(),output_path.c_str(),source_path.c_str(),script.c_str(),nullptr,report))
    throw std::runtime_error("Map preparation failed. See wwhd-minimap.log. If game preparation ran this session, restart the app and try again.");
   if(!ready(utf8(output_path)))throw std::runtime_error("Prepared map validation failed; previous maps were preserved.");
   {std::lock_guard lock(config_mutex);config.data=utf8(output_path);config.enabled=true;config.renderer=0;save();}
   note("Maps prepared. Close and reopen the app to enable the minimap.");
  }catch(const std::exception& e){note(e.what());report(nullptr,e.what(),strlen(e.what()));}
  catch(winrt::hresult_error const& e){note("Storage access failed. Check the source connection.");}
  winrt::uninit_apartment();busy=false;
 });
}
void draw_settings(){
 if(std::getenv("WWHD_TEST_MINIMAP_UI"))ImGui::SetNextItemOpen(true,ImGuiCond_Once);
 if(!ImGui::CollapsingHeader("Minimap"))return;
 bool changed=false,gamecube=true;
 {std::lock_guard lock(config_mutex);
  changed|=ImGui::Checkbox("Enable minimap",&config.enabled);
#ifdef WWHD_BUILTIN_HD_MINIMAP
  if(hd_only)ImGui::TextUnformatted("Minimap HD");
  else changed|=ImGui::Combo("Minimap renderer",&config.renderer,"GameCube minimap\0Minimap HD\0");
#endif
  gamecube=config.renderer==0;
  if(gamecube){
   ImGui::TextWrapped("Prepare maps from your own GameCube Wind Waker ISO, Zstandard RVZ or extracted folder. The source is read in place.");
   changed|=ImGui::Checkbox("Show dungeon maps",&config.dungeons);
   changed|=ImGui::Combo("Map style",&config.style,"Vector\0Original\0");
   changed|=ImGui::SliderFloat("Minimap size",&config.size,64,600,"%.0f");
   changed|=ImGui::SliderFloat("Minimap opacity",&config.opacity,0,1,"%.2f");
   changed|=ImGui::SliderFloat("Minimap left margin",&config.x,0,1200,"%.0f");
   changed|=ImGui::SliderFloat("Minimap bottom margin",&config.y,0,650,"%.0f");
   if(config.enabled&&!ready(config.data))ImGui::TextWrapped("No complete local maps found. Prepare a GameCube source first.");
  }else{
   ImGui::TextWrapped("Minimap HD 1.3 by Chironeyes. Uses your Wii U game's native maps and markers; no GameCube source is needed. USA v0 only.");
   changed|=ImGui::SliderFloat("HD minimap size",&config.hd_size,120,240,"%.0f");
   changed|=ImGui::Checkbox("Fit full island while sailing",&config.hd_fit_islands);
   changed|=ImGui::Checkbox("Fade while moving or turning the camera",&config.hd_fade);
   changed|=ImGui::SliderFloat("Idle map opacity",&config.hd_max,0,1,"%.2f");
   changed|=ImGui::SliderFloat("Moving map opacity",&config.hd_min,0,config.hd_max,"%.2f");
   config.hd_min=std::min(config.hd_min,config.hd_max);
   changed|=ImGui::Checkbox("Show map background",&config.hd_background);
   changed|=ImGui::SliderFloat("Background opacity",&config.hd_background_opacity,0,1,"%.2f");
   changed|=ImGui::Checkbox("Show wind direction",&config.hd_wind);
   ImGui::TextWrapped("The wind arrow follows the current wind relative to north; turning the boat does not change the wind.");
   ImGui::TextWrapped("On foot: D-pad left hides/shows the map; right changes its size. Sailing: D-pad down cycles compasses, map and owned treasure charts. L3 cycles charts. Dungeon maps require the Map item; markers follow native Compass rules.");
  }
  if(changed)try{save();note("Restart the app to apply minimap changes.");}catch(const std::exception& e){note(e.what());}
 }
 if(gamecube){
  ImGui::BeginDisabled(busy.load());
  if(ImGui::Button("Select GameCube ISO / RVZ"))uwp_browser::choose_game_cube(false,begin_preparation);
  ImGui::SameLine();if(ImGui::Button("Select extracted GameCube folder"))uwp_browser::choose_game_cube(true,begin_preparation);
  ImGui::EndDisabled();
 }
 if(busy.load()){ImGui::Text("Preparing maps: %u files written",written.load());if(ImGui::Button("Cancel map preparation"))cancelled=true;}
 {std::lock_guard lock(status_mutex);if(!message.empty())ImGui::TextWrapped("%s",message.c_str());}
 if(worker.valid()&&!busy.load()&&worker.wait_for(std::chrono::seconds(0))==std::future_status::ready)worker.get();
}
}
#endif
