// SPDX-License-Identifier: MPL-2.0
// Private experimental host of the upstream game runtime, not the graphics diagnostic.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <windows.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <thread>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
int wwhd_game_main(int,char**);
// Initial USB convention for the experimental build; assets stay on the drive.
static std::filesystem::path find_usb_game(){
 std::filesystem::path result;
 std::thread worker([&]{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  try{
   auto drives=winrt::Windows::Storage::KnownFolders::RemovableDevices().GetFoldersAsync().get();
   for(auto const& drive:drives){
    std::vector<winrt::Windows::Storage::StorageFolder> candidates;
    try{candidates.push_back(drive.GetFolderAsync(L"WWHD").get());}catch(winrt::hresult_error const&){}
    candidates.push_back(drive);
    for(auto const& folder:candidates){
     try{
      auto code=folder.GetFolderAsync(L"code").get();auto rpx=code.GetFileAsync(L"cking.rpx").get();
      (void)folder.GetFolderAsync(L"content").get();(void)folder.GetFolderAsync(L"meta").get();
      winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().AddOrReplace(L"wwhd-game",folder);
      FILE* probe=nullptr;auto path=std::wstring(rpx.Path());
      if(_wfopen_s(&probe,path.c_str(),L"rb")!=0||!probe)continue;
      unsigned char header[4]{};auto read=fread(header,1,4,probe);fclose(probe);
      if(read!=4||header[0]!=0x7f||header[1]!='E'||header[2]!='L'||header[3]!='F')continue;
      result=std::wstring(folder.Path());break;
     }catch(winrt::hresult_error const&){}
    }
    if(!result.empty())break;
   }
  }catch(winrt::hresult_error const&){}
  winrt::uninit_apartment();
 });worker.join();return result;
}

static int SDLCALL run_game(int,char**){
 try{
  auto local=std::filesystem::path(std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()));
  auto user=local/L"WWHD";std::filesystem::create_directories(user/L"captures");
  std::filesystem::current_path(user);
  auto game=std::filesystem::path(std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path()))/L"game";
  if(std::filesystem::is_regular_file(user/L"game"/L"code"/L"cking.rpx"))game=user/L"game";
  std::ifstream chosen(local/L"game-path.txt");std::string selected;if(chosen&&std::getline(chosen,selected)&&!selected.empty())game=std::filesystem::u8path(selected);
  const auto as_utf8=[](const std::filesystem::path& p){auto text=p.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());};
  auto gamePath=as_utf8(game),savePath=as_utf8(user/L"save"),logPath=as_utf8(user/L"wwhd.log");
  _putenv_s("WWHD_LOG_FILE",logPath.c_str());_putenv_s("WWHD_NO_GAMEPAD","1");
  _putenv_s("WWHD_FACE_AUTO","1");
  _putenv_s("SDL_JOYSTICK_WGI","1");
  _putenv_s("SDL_JOYSTICK_RAWINPUT","0");
  SDL_SetHint(SDL_HINT_JOYSTICK_WGI,"1");
  SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT,"0");
  // Debug layers are opt-in via LocalState/dzn-debug.txt; keep normal play uninstrumented.
  _putenv_s("DZN_DEBUG","");
  // Let Dozen select barriers from the hardware capabilities (Xbox already reports legacy only).
  std::ifstream dznDbg(local/L"dzn-debug.txt");std::string dznFlags;if(dznDbg&&std::getline(dznDbg,dznFlags)&&!dznFlags.empty())_putenv_s("DZN_DEBUG",dznFlags.c_str());
  FILE* stream=nullptr;freopen_s(&stream,as_utf8(user/L"wwhd-console.log").c_str(),"w",stderr);
  // Diagnostic build: aggregate reports by default; no graphics or interpolation settings change.
  // LocalState/performance-mode.txt: cpu (default), detailed (per-draw/pass), off (baseline).
  std::string perfMode="cpu",requestedMode;
  std::ifstream perfFile(local/L"performance-mode.txt");
  if(perfFile&&std::getline(perfFile,requestedMode)) {
   if(!requestedMode.empty()&&requestedMode.back()=='\r')requestedMode.pop_back();
   if(requestedMode=="cpu"||requestedMode=="detailed"||requestedMode=="off")perfMode=requestedMode;
  }
  const bool profiling=perfMode!="off",detailed=perfMode=="detailed";
  _putenv_s("WWHD_PROFILE",profiling?"1":"");
  _putenv_s("WWHD_VK_CPU_ONLY_STATS",profiling?"1":"");
  _putenv_s("WWHD_INTERP_PASS_STATS",profiling?"1":"");
  _putenv_s("WWHD_VK_GPU_TIMESTAMPS",profiling?"1":"");
  _putenv_s("WWHD_VK_STATS",detailed?"1":"");
  _putenv_s("WWHD_VK_GPU_PASS_TIMESTAMPS",detailed?"1":"");
  fprintf(stderr,"[uwp diagnostics] performance mode: %s; settings unchanged; reports add measurement overhead\n",perfMode.c_str());
  std::ifstream hashModeFile(local/L"texture-hash-mode.txt");std::string hashMode;
  if(hashModeFile&&std::getline(hashModeFile,hashMode)) {
   if(!hashMode.empty()&&hashMode.back()=='\r')hashMode.pop_back();
   if(hashMode=="baseline")_putenv_s("WWHD_UWP_TEXTURE_HASH","baseline");
  }
  std::ifstream vertexModeFile(local/L"vertex-cache-mode.txt");std::string vertexMode;
  if(vertexModeFile&&std::getline(vertexModeFile,vertexMode)) {
   if(!vertexMode.empty()&&vertexMode.back()=='\r')vertexMode.pop_back();
   if(vertexMode=="baseline")_putenv_s("WWHD_UWP_VERTEX_CACHE","0");
  }
  std::vector<std::string> arguments{"wwhd-uwp-game","--game",gamePath,"--save",savePath,"--renderer=vulkan"};
  std::ifstream mode(local/L"test-mode.txt");std::string testMode;if(mode&&std::getline(mode,testMode)&&testMode=="renderer-smoke")arguments.emplace_back("--renderer-smoke");
  if(testMode=="boot-capture"){
   _putenv_s("WWHD_DUMP_FRAMES","1,30,120");_putenv_s("WWHD_DUMP_PRESENT","1");_putenv_s("WWHD_EXIT_AT_FRAME","180");
  }
  std::vector<char*> argv;for(auto& arg:arguments)argv.push_back(arg.data());argv.push_back(nullptr);
  if(testMode!="renderer-smoke"&&!std::filesystem::is_regular_file(game/L"code"/L"cking.rpx")){
   auto usb=find_usb_game();
   if(usb.empty())throw std::runtime_error("No extracted WWHD game found. Put your USA v0 code, content and meta folders in USB:\\WWHD, or LocalState/WWHD/game. Extract WUD/WUX/WUA images on the PC first.");
   arguments[2]=as_utf8(usb);argv.clear();for(auto& arg:arguments)argv.push_back(arg.data());argv.push_back(nullptr);
  }
  return wwhd_game_main(int(arguments.size()),argv.data());
 }catch(std::exception const& e){OutputDebugStringA(e.what());SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"WWHD UWP startup",e.what(),nullptr);return 1;}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){return SDL_RunApp(0,nullptr,run_game,nullptr);}
