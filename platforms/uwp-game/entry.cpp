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
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include "platform/uwp_write_guard.h"
#include "mods/uwp_gc_minimap.h"
#ifdef WWHD_DYNAMIC_GAMECODE
#include "dynamic_game.h"
#ifdef WWHD_UNIFIED_PREPARATION
#include "preparation.h"
#endif
#include <winrt/Windows.System.h>
#endif
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

#ifdef WWHD_DYNAMIC_GAMECODE
struct PreparedGame {std::filesystem::path game,objects;};
static PreparedGame find_dynamic_game(){
 PreparedGame result;std::exception_ptr failure;
 std::thread worker([&]{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  try{
   using namespace winrt::Windows::Storage;
   std::vector<StorageFolder> roots;
   auto local=ApplicationData::Current().LocalFolder();
   // Local validation configuration is never included in the APPX.
   try{auto config=local.GetFileAsync(L"dynamic-test-root.txt").get();roots.push_back(StorageFolder::GetFolderFromPathAsync(FileIO::ReadTextAsync(config).get()).get());}catch(winrt::hresult_error const&){}
   for(auto const& drive:KnownFolders::RemovableDevices().GetFoldersAsync().get())roots.push_back(drive);
   for(auto const& drive:roots){
    StorageFolder prep{nullptr};
    try{prep=drive.GetFolderAsync(L"WWHD-UWP-Preparation").get();}catch(winrt::hresult_error const&){continue;}
    for(auto const& job:prep.GetFoldersAsync().get()){
     if(std::wstring(job.Name().c_str()).rfind(L"import",0)!=0)continue;
     try{
      auto game=job.GetFolderAsync(L"game").get();auto code=game.GetFolderAsync(L"code").get();(void)code.GetFileAsync(L"cking.rpx").get();
      (void)game.GetFolderAsync(L"content").get();(void)game.GetFolderAsync(L"meta").get();
      auto generated=job.GetFolderAsync(L"generated").get();
      for(auto const& objects:generated.GetFoldersAsync().get()){
       if(std::wstring(objects.Name().c_str()).rfind(L"objects",0)!=0)continue;
       try{(void)objects.GetFileAsync(L"compilation-complete.json").get();}catch(winrt::hresult_error const&){continue;}
       winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().AddOrReplace(L"wwhd-dynamic-game",game);
       result.game=std::wstring(game.Path());result.objects=std::wstring(objects.Path());break;
      }
     }catch(winrt::hresult_error const&){}
     if(!result.game.empty())break;
    }
    if(!result.game.empty())break;
   }
  }catch(winrt::hresult_error const& e){failure=std::make_exception_ptr(std::runtime_error(winrt::to_string(e.message())));}
   catch(...){failure=std::current_exception();}
  winrt::uninit_apartment();
 });worker.join();if(failure)std::rethrow_exception(failure);return result;
}
#endif

static int SDLCALL run_game(int,char**){
 try{
  auto local=std::filesystem::path(std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()));
  const auto installedPath=std::filesystem::path(std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path()));
  // Clean console packages retain saved settings and game paths, but never
  // inherit old A/B selectors, frame-exit scripts or smoke-test launch modes.
  const bool cleanRuntime=std::filesystem::is_regular_file(installedPath/L"clean-runtime.txt");
  const auto selectorRoot=cleanRuntime?installedPath/L"_no_diagnostics":local;
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
  std::ifstream dznDbg(selectorRoot/L"dzn-debug.txt");std::string dznFlags;if(dznDbg&&std::getline(dznDbg,dznFlags)&&!dznFlags.empty())_putenv_s("DZN_DEBUG",dznFlags.c_str());
  FILE* stream=nullptr;freopen_s(&stream,as_utf8(user/L"wwhd-console.log").c_str(),"w",stderr);
  // Diagnostic build: aggregate reports by default; no graphics or interpolation settings change.
  // LocalState/performance-mode.txt: cpu (default), detailed (per-draw/pass), off (baseline).
  std::string perfMode=
#ifdef WWHD_UNIFIED_PREPARATION
  "off";
#else
  "cpu";
#endif
  // Private diagnostic packages can request counters without changing saved settings.
  std::ifstream perfDefault(installedPath/L"performance-mode-default.txt");
  std::string perfDefaultMode;
  if(perfDefault && std::getline(perfDefault,perfDefaultMode)) {
   if(!perfDefaultMode.empty() && perfDefaultMode.back()=='\r')perfDefaultMode.pop_back();
   if(perfDefaultMode=="cpu" || perfDefaultMode=="detailed" || perfDefaultMode=="off")perfMode=perfDefaultMode;
  }
  std::string requestedMode;
  std::ifstream perfFile(selectorRoot/L"performance-mode.txt");
  if(perfFile&&std::getline(perfFile,requestedMode)) {
   if(!requestedMode.empty()&&requestedMode.back()=='\r')requestedMode.pop_back();
   if(requestedMode=="cpu"||requestedMode=="detailed"||requestedMode=="off")perfMode=requestedMode;
  }
  std::string submissionTest,submissionRequest;
  auto readSubmissionTest=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,submissionRequest)) {
    if(!submissionRequest.empty() && submissionRequest.back()=='\r')submissionRequest.pop_back();
    if(submissionRequest=="measure-512" || submissionRequest=="measure-1024" || submissionRequest=="off-512")submissionTest=submissionRequest;
   }
  };
  readSubmissionTest(installedPath/L"submission-test-default.txt");
  readSubmissionTest(selectorRoot/L"submission-test.txt");
  if(!submissionTest.empty()) {
   const bool measured=submissionTest!="off-512";
   perfMode=measured?"cpu":"off";
   _putenv_s("WWHD_VK_DRAW_BATCH",submissionTest=="measure-1024"?"1024":"512");
   _putenv_s("WWHD_VK_SUBMIT_STATS",measured?"1":"0");
   _putenv_s("WWHD_VK_CADENCE_ONLY_STATS",measured?"0":"1");
   fprintf(stderr,"[uwp submission test] %s; explicit test overrides diagnostic counter mode; graphics settings unchanged\n",submissionTest.c_str());
  }
  // Focused private experiment wins over older diagnostic selector files.
  std::string renderAnalysis,analysisRequest;
  auto readRenderAnalysis=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,analysisRequest)) {
    if(!analysisRequest.empty() && analysisRequest.back()=='\r')analysisRequest.pop_back();
    if(analysisRequest=="measure" || analysisRequest=="light")renderAnalysis=analysisRequest;
   }
  };
  readRenderAnalysis(installedPath/L"render-analysis-default.txt");
  readRenderAnalysis(selectorRoot/L"render-analysis.txt");
  if(!renderAnalysis.empty()) {
   const bool measure=renderAnalysis=="measure";
   perfMode=measure?"detailed":"off";
   _putenv_s("WWHD_VK_DRAW_BATCH","512");
   _putenv_s("WWHD_VK_SUBMIT_STATS",measure?"1":"0");
   _putenv_s("WWHD_VK_CADENCE_ONLY_STATS",measure?"0":"1");
   fprintf(stderr,"[uwp render analysis] %s; overrides older diagnostic selectors; batch 512; rendering options unchanged\n",renderAnalysis.c_str());
  }
  std::string commandAnalysis,commandRequest;
  auto readCommandAnalysis=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,commandRequest)) {
    if(!commandRequest.empty() && commandRequest.back()=='\r')commandRequest.pop_back();
    if(commandRequest=="measure" || commandRequest=="light")commandAnalysis=commandRequest;
   }
  };
  readCommandAnalysis(installedPath/L"command-analysis-default.txt");
  readCommandAnalysis(selectorRoot/L"command-analysis.txt");
  const bool traceCommands=commandAnalysis=="measure";
  _putenv_s("WWHD_VK_COMMAND_TRACE",traceCommands?"1":"0");
  if(!commandAnalysis.empty()) {
   perfMode=traceCommands?"cpu":"off";
   _putenv_s("WWHD_VK_DRAW_BATCH","512");
   _putenv_s("WWHD_VK_SUBMIT_STATS",traceCommands?"1":"0");
   _putenv_s("WWHD_VK_CADENCE_ONLY_STATS",traceCommands?"0":"1");
   fprintf(stderr,"[uwp command analysis] %s; overrides older diagnostics; batch 512; uniform experiment forced baseline; no graphics settings changed\n",commandAnalysis.c_str());
  }
  std::string samplerCacheMode="baseline",samplerRequest;
  bool samplerCacheActive=false;
  auto readSamplerCache=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,samplerRequest)) {
    if(!samplerRequest.empty() && samplerRequest.back()=='\r')samplerRequest.pop_back();
    if(samplerRequest=="baseline" || samplerRequest=="optimized" || samplerRequest=="verify") {
     samplerCacheMode=samplerRequest;samplerCacheActive=true;
    }
   }
  };
  readSamplerCache(installedPath/L"sampler-cache-default.txt");
  readSamplerCache(selectorRoot/L"sampler-cache-mode.txt");
  _putenv_s("WWHD_VK_SAMPLER_SLOTS",samplerCacheMode.c_str());
  std::ifstream writeGuardFile(selectorRoot/L"write-guard-mode.txt");std::string writeGuardMode;
  if(std::getline(writeGuardFile,writeGuardMode)) {
   if(!writeGuardMode.empty()&&writeGuardMode.back()=='\r')writeGuardMode.pop_back();
   if(writeGuardMode=="baseline" || writeGuardMode=="optimized" || writeGuardMode=="verify" || writeGuardMode=="window-verify") {
    _putenv_s("WWHD_UWP_WRITE_GUARD",writeGuardMode=="baseline"?"baseline":"optimized");
    _putenv_s("WWHD_VK_BUFFER_CACHE_VERIFY",writeGuardMode=="verify"?"1":"0");
    _putenv_s("WWHD_UWP_CACHED_WINDOW_VERIFY",writeGuardMode=="window-verify"?"1":"0");
    if(writeGuardMode!="baseline")_putenv_s("WWHD_VK_BUFFER_CACHE_MB","64");
   }
  }
  std::ifstream rawCacheFile(selectorRoot/L"persistent-raw-mode.txt");std::string rawCacheMode;
  if(std::getline(rawCacheFile,rawCacheMode)) {
   if(!rawCacheMode.empty()&&rawCacheMode.back()=='\r')rawCacheMode.pop_back();
   if(rawCacheMode=="baseline" || rawCacheMode=="optimized" || rawCacheMode=="verify")
    _putenv_s("WWHD_UWP_PERSISTENT_RAW",rawCacheMode.c_str());
  }
  std::ifstream nativeWatchFile(selectorRoot/L"native-texture-watch-mode.txt");std::string nativeWatchMode;
  if(std::getline(nativeWatchFile,nativeWatchMode)) {
   if(!nativeWatchMode.empty()&&nativeWatchMode.back()=='\r')nativeWatchMode.pop_back();
   if(nativeWatchMode=="baseline" || nativeWatchMode=="optimized" || nativeWatchMode=="verify")
    _putenv_s("WWHD_UWP_NATIVE_TEXTURE_WATCH",nativeWatchMode.c_str());
  }
  if(samplerCacheActive) {
   perfMode="cpu";
   _putenv_s("WWHD_VK_COMMAND_TRACE","0");
   _putenv_s("WWHD_VK_DRAW_BATCH","512");
   _putenv_s("WWHD_VK_SUBMIT_STATS","1");
   _putenv_s("WWHD_VK_CADENCE_ONLY_STATS","0");
   fprintf(stderr,"[uwp sampler cache] %s; overrides old diagnostic selectors; command trace off; uniform experiment baseline; graphics unchanged\n",samplerCacheMode.c_str());
  }
  const bool profiling=perfMode!="off",detailed=perfMode=="detailed";
  // An unset WWHD_PROFILE defaults to enabled upstream; off must explicitly be 0.
  _putenv_s("WWHD_PROFILE",profiling?"1":"0");
  _putenv_s("WWHD_VK_CPU_ONLY_STATS",profiling?"1":"");
  _putenv_s("WWHD_INTERP_PASS_STATS",profiling?"1":"");
  _putenv_s("WWHD_VK_GPU_TIMESTAMPS",profiling?"1":"");
  _putenv_s("WWHD_VK_STATS",detailed?"1":"");
  _putenv_s("WWHD_VK_GPU_PASS_TIMESTAMPS",detailed && renderAnalysis.empty()?"1":"");
  fprintf(stderr,"[uwp diagnostics] performance mode: %s; settings unchanged; reports add measurement overhead\n",perfMode.c_str());
  // A/B testing uses the same runtime and caches. LocalState overrides the
  // packaged default; no saved graphics, game-speed or preparation settings change.
  std::string drawStateMode = "optimized", drawStateRequest;
  const auto validDrawMode=[](const std::string& value) {
   return value=="baseline" || value=="optimized" || value=="verify";
  };
  auto readDrawMode=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,drawStateRequest)) {
    if(!drawStateRequest.empty() && drawStateRequest.back()=='\r')drawStateRequest.pop_back();
    if(validDrawMode(drawStateRequest))drawStateMode=drawStateRequest;
   }
  };
  readDrawMode(installedPath/L"draw-state-default.txt");
  readDrawMode(selectorRoot/L"draw-state-mode.txt");
  _putenv_s("WWHD_VK_FASTPATH",drawStateMode=="baseline"?"0":"1");
  _putenv_s("WWHD_VK_FASTPATH_VERIFY",drawStateMode=="verify"?"1":"");
  fprintf(stderr,"[uwp draw state] mode: %s; preparation profile and graphics settings unchanged\n",drawStateMode.c_str());
  std::string vertexCopyMode="optimized",vertexCopyRequest;
  auto readVertexCopyMode=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,vertexCopyRequest)) {
    if(!vertexCopyRequest.empty() && vertexCopyRequest.back()=='\r')vertexCopyRequest.pop_back();
    if(vertexCopyRequest=="baseline" || vertexCopyRequest=="optimized")vertexCopyMode=vertexCopyRequest;
   }
  };
  readVertexCopyMode(installedPath/L"vertex-copy-default.txt");
  readVertexCopyMode(selectorRoot/L"vertex-copy-mode.txt");
  const bool vertexWindows=vertexCopyMode=="optimized";
  _putenv_s("WWHD_VK_VERTEX_COPY_WINDOW",vertexWindows?"1":"0");
  _putenv_s("WWHD_UWP_VERTEX_WINDOWS",vertexWindows?"1":"0");
  fprintf(stderr,"[uwp vertex copies] mode: %s; exact reachable windows, unchanged vertex IDs; graphics settings unchanged\n",vertexCopyMode.c_str());
  std::string poolResetMode="optimized",poolResetRequest;
  auto readPoolResetMode=[&](const std::filesystem::path& path) {
   std::ifstream file(path);
   if(file && std::getline(file,poolResetRequest)) {
    if(!poolResetRequest.empty() && poolResetRequest.back()=='\r')poolResetRequest.pop_back();
    if(poolResetRequest=="baseline" || poolResetRequest=="optimized")poolResetMode=poolResetRequest;
   }
  };
  readPoolResetMode(installedPath/L"pool-reset-default.txt");
  readPoolResetMode(selectorRoot/L"pool-reset-mode.txt");
  _putenv_s("WWHD_UWP_DZN_RESET_BASELINE",poolResetMode=="baseline"?"1":"0");
  fprintf(stderr,"[uwp descriptor reset] mode: %s; fenced reset only; graphics settings unchanged\n",poolResetMode.c_str());
  std::string pacingMode="optimized",pacingRequest;
  std::ifstream pacingFile(selectorRoot/L"interpolation-clock-mode.txt");
  if(pacingFile && std::getline(pacingFile,pacingRequest)) {
   if(!pacingRequest.empty() && pacingRequest.back()=='\r')pacingRequest.pop_back();
   if(pacingRequest=="baseline" || pacingRequest=="optimized")pacingMode=pacingRequest;
  }
  _putenv_s("WWHD_INTERP_CLOCK_SCHEDULE",pacingMode=="baseline"?"0":"1");
  fprintf(stderr,"[uwp interpolation clock] mode: %s; original logic rate unchanged; applies when Keep game speed is on\n",pacingMode.c_str());
  std::string uniformPackMode="baseline",uniformPackRequest;
  std::ifstream uniformPackFile(selectorRoot/L"uniform-pack-mode.txt");
  if(uniformPackFile && std::getline(uniformPackFile,uniformPackRequest)) {
   if(!uniformPackRequest.empty() && uniformPackRequest.back()=='\r')uniformPackRequest.pop_back();
   if(uniformPackRequest=="baseline" || uniformPackRequest=="optimized" || uniformPackRequest=="verify")uniformPackMode=uniformPackRequest;
  }
  if(!commandAnalysis.empty() || samplerCacheActive)uniformPackMode="baseline";
  _putenv_s("WWHD_VK_UNIFORM_PACK",uniformPackMode.c_str());
  fprintf(stderr,"[uwp uniform packing] %s; exact register reuse plus fixed layout; guest data read fresh; stereo uses original packer\n",uniformPackMode.c_str());
  std::ifstream hashModeFile(selectorRoot/L"texture-hash-mode.txt");std::string hashMode;
  if(hashModeFile&&std::getline(hashModeFile,hashMode)) {
   if(!hashMode.empty()&&hashMode.back()=='\r')hashMode.pop_back();
   if(hashMode=="baseline")_putenv_s("WWHD_UWP_TEXTURE_HASH","baseline");
  }
  std::ifstream vertexModeFile(selectorRoot/L"vertex-cache-mode.txt");std::string vertexMode;
  if(vertexModeFile&&std::getline(vertexModeFile,vertexMode)) {
   if(!vertexMode.empty()&&vertexMode.back()=='\r')vertexMode.pop_back();
   if(vertexMode=="baseline")_putenv_s("WWHD_UWP_VERTEX_CACHE","0");
  }
  // Opt-in packaged console comparison profile. Apply after historical diagnostic
  // selectors so stale LocalState files cannot silently disable this experiment.
  std::string bufferReuseMode;
  const auto readBufferReuse=[&](const std::filesystem::path& path) {
   std::ifstream file(path);std::string value;
   if(std::getline(file,value)) {
    if(!value.empty()&&value.back()=='\r')value.pop_back();
    if(value=="baseline"||value=="optimized")bufferReuseMode=value;
   }
  };
  readBufferReuse(installedPath/L"buffer-reuse-default.txt");
  readBufferReuse(selectorRoot/L"buffer-reuse-mode.txt");
  if(!bufferReuseMode.empty()) {
   _putenv_s("WWHD_UWP_WRITE_GUARD",bufferReuseMode=="optimized"?"optimized":"baseline");
   _putenv_s("WWHD_UWP_NATIVE_TEXTURE_WATCH","optimized");
   _putenv_s("WWHD_UWP_PERSISTENT_RAW","baseline");
   _putenv_s("WWHD_VK_BUFFER_CACHE","1");
   _putenv_s("WWHD_VK_BUFFER_CACHE_MB","64");
   _putenv_s("WWHD_VK_BUFFER_CACHE_VERIFY","0");
   _putenv_s("WWHD_UWP_CACHED_WINDOW_VERIFY","0");
   _putenv_s("WWHD_VK_DRAW_BATCH","4096");
   _putenv_s("WWHD_UWP_INDEX_MEMO","baseline");
   _putenv_s("WWHD_VK_SAMPLER_SLOTS","baseline");
   _putenv_s("WWHD_VK_UNIFORM_PACK","baseline");
   _putenv_s("WWHD_VK_FASTPATH","1");
   _putenv_s("WWHD_VK_FASTPATH_VERIFY","0");
   _putenv_s("WWHD_VK_VERTEX_COPY_WINDOW","1");
   _putenv_s("WWHD_UWP_VERTEX_WINDOWS","1");
   _putenv_s("WWHD_UWP_VERTEX_CACHE","1");
   _putenv_s("WWHD_UWP_TEXTURE_HASH","optimized");
   _putenv_s("WWHD_UWP_DZN_RESET_BASELINE","0");
   _putenv_s("WWHD_INTERP_CLOCK_SCHEDULE","1");
   _putenv_s("WWHD_VK_COMMAND_TRACE","0");
   _putenv_s("WWHD_PROFILE","1");
   _putenv_s("WWHD_VK_CPU_ONLY_STATS","1");
   _putenv_s("WWHD_INTERP_PASS_STATS","1");
   _putenv_s("WWHD_VK_GPU_TIMESTAMPS","1");
   _putenv_s("WWHD_VK_STATS","");
   _putenv_s("WWHD_VK_GPU_PASS_TIMESTAMPS","");
   _putenv_s("WWHD_VK_SUBMIT_STATS","1");
   _putenv_s("WWHD_VK_CADENCE_ONLY_STATS","0");
   fprintf(stderr,"[uwp buffer reuse test] %s; batch 4096; original cache protocol; verification off; saved graphics and logic unchanged\n",bufferReuseMode.c_str());
  }
  std::ifstream gatherFile(selectorRoot/L"uniform-gather-mode.txt");std::string gatherMode;
  if(std::getline(gatherFile,gatherMode)) {
   if(!gatherMode.empty()&&gatherMode.back()=='\r')gatherMode.pop_back();
   if(gatherMode=="baseline"||gatherMode=="optimized"||gatherMode=="verify") {
    _putenv_s("WWHD_VK_UNIFORM_PACK",gatherMode=="verify"?"gather-verify":gatherMode=="optimized"?"gather":"baseline");
    fprintf(stderr,"[uwp uniform gather] %s; complete source bytes; unchanged shader layout\n",gatherMode.c_str());
   }
  }
  const auto readGrassMode=[&](const std::filesystem::path& path) {
   std::ifstream grassFile(path);std::string grassMode;
   if(std::getline(grassFile,grassMode)) {
    if(!grassMode.empty()&&grassMode.back()=='\r')grassMode.pop_back();
    if(grassMode=="off"||grassMode=="ready"||grassMode=="measure"||grassMode=="distance")_putenv_s("WWHD_UWP_GRASS_MODE",grassMode.c_str());
   }
  };
  readGrassMode(installedPath/L"grass-experiment-default.txt");
  readGrassMode(selectorRoot/L"decorative-distance-mode.txt");
  std::ifstream distanceFile(selectorRoot/L"grass-distance.txt");std::string grassDistance;
  if(std::getline(distanceFile,grassDistance)) {
   if(!grassDistance.empty()&&grassDistance.back()=='\r')grassDistance.pop_back();
   _putenv_s("WWHD_UWP_GRASS_DISTANCE",grassDistance.c_str());
  }
  std::vector<std::string> arguments{"wwhd-uwp-game","--game",gamePath,"--save",savePath,"--renderer=vulkan"};
  std::ifstream mode(selectorRoot/L"test-mode.txt");std::string testMode;if(mode&&std::getline(mode,testMode)&&testMode=="renderer-smoke")arguments.emplace_back("--renderer-smoke");
  // Explicit local PC research fixture, never enabled by package defaults.
  // Uses the isolated test identity's save data, not the playable PC apps.
  if(testMode=="pc-benchmark") {
   const std::vector<std::string> allowed={"WWHD_PRESS","WWHD_STATE_SAVE_AT","WWHD_STATE_LOAD_AT",
    "WWHD_DUMP_FRAMES","WWHD_EXIT_AT_FRAME","WWHD_TEST_ORIGIN","WWHD_TEST_STICK","WWHD_TEST_RSTICK",
    "WWHD_LOG_FLIP_PACING","WWHD_VK_SAMPLER_SLOTS","WWHD_UWP_NATIVE_TEXTURE_WATCH","WWHD_UWP_INDEX_MEMO",
    "WWHD_VK_DRAW_BATCH","WWHD_VK_DRAW_BATCH_CAP","WWHD_VK_BUFFER_CACHE_VERIFY","WWHD_VK_BUFFER_CACHE_MB",
    "WWHD_VK_REUSE_UNIFORM_SNAPSHOTS","WWHD_VK_UNIFORM_PACK","WWHD_VK_STATS",
    "WWHD_VK_SHADER_KEY_DIRTY","WWHD_VK_SHADER_STATE_MEMO","WWHD_VK_DESCRIPTOR_RANKS",
    "WWHD_VK_UNIFORM_TIMING","WWHD_TEST_OVERLAY","WWHD_UWP_GRASS_MODE","WWHD_UWP_GRASS_DISTANCE",
    "WWHD_TEST_GRASS_TOGGLE","WWHD_TEST_EFFECTS_SCROLL",
    "WWHD_UWP_GC_MINIMAP_DATA","WWHD_UWP_GC_MINIMAP_PACKAGE","WWHD_TEST_MINIMAP_PREPARE","WWHD_TEST_MINIMAP_UI",
    "WWHD_VK_FEEDBACK_STATS","WWHD_VK_FEEDBACK_ALLOC_LOG","WWHD_VK_REUSE_FEEDBACK_IMAGES",
    "WWHD_VK_GPU_TIMESTAMPS","WWHD_VK_GPU_PASS_TIMESTAMPS",
    "WWHD_VK_FASTPATH_VERIFY","WWHD_VK_ALU_REG_BATCH","WWHD_UWP_HD_MINIMAP",
    "WWHD_VK_FASTPATH","WWHD_VK_BUFFER_CACHE","WWHD_VK_VERTEX_COPY_WINDOW","WWHD_UWP_VERTEX_WINDOWS",
    "WWHD_ASPECT_TRACE_FRAME", "WWHD_UWP_HD_MINIMAP_DIAGNOSTIC"};
   _putenv_s("WWHD_EXIT_AT_FRAME","1800");
   _putenv_s("WWHD_NO_HOST_INPUT","1");
   _putenv_s("WWHD_DUMP_PRESENT","1");
   // Explicit private preferences override NO_HOST_INPUT's default of ignoring
   // saved settings. Quality/mode setters run at normal UI initialization;
   // setting their environment defaults here would be too late for globals.
   _putenv_s("WWHD_SETTINGS",as_utf8(user/L"settings.ini").c_str());
   std::ifstream fixture(selectorRoot/L"pc-benchmark.txt");std::string line;
   while(std::getline(fixture,line)) {
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    auto equal=line.find('=');if(equal==std::string::npos)continue;
    auto key=line.substr(0,equal),value=line.substr(equal+1);
    if(std::find(allowed.begin(),allowed.end(),key)!=allowed.end()) {
     _putenv_s(key.c_str(),value.c_str());
     fprintf(stderr,"[uwp pc benchmark] %s=%s\n",key.c_str(),value.c_str());
    }
   }
  }
  if(testMode=="boot-capture"||testMode=="boot-trace"||testMode=="boot-capture-long"){
   if(testMode=="boot-trace"){arguments.emplace_back("--trace");g_ppc_trace=1;_putenv_s("WWHD_BOOTDBG","1");}
   _putenv_s("WWHD_DUMP_FRAMES",testMode=="boot-capture-long"?"1,120,600":"1,30,120");_putenv_s("WWHD_DUMP_PRESENT","1");_putenv_s("WWHD_EXIT_AT_FRAME",testMode=="boot-capture-long"?"900":"180");
  }
  // Explicit diagnostic only. Exercise missed interpolation deadlines with the
  // upstream render-load hook; never enabled during normal play or by defaults.
  if(testMode=="boot-pacing-load") {
   _putenv_s("WWHD_TEST_RENDER_LOAD_MS","22");
   _putenv_s("WWHD_EXIT_AT_FRAME","1200");
   fprintf(stderr,"[uwp pacing test] explicit synthetic render load 22 ms/frame; not a hardware simulation\n");
  }
  std::vector<char*> argv;for(auto& arg:arguments)argv.push_back(arg.data());argv.push_back(nullptr);
#ifdef WWHD_DYNAMIC_GAMECODE
  if(winrt::Windows::System::MemoryManager::AppMemoryUsageLimit()<(uint64_t(2)<<30))throw std::runtime_error("Set this application to Game in Dev Home and restart.");
  #ifdef WWHD_UNIFIED_PREPARATION
  auto prepared=wwhd_prepare_and_load();
  if(prepared.game.empty())return 0;
#else
  auto prepared=find_dynamic_game();
  if(prepared.game.empty())throw std::runtime_error("No completed USB preparation found. Keep WWHD-UWP-Preparation from the 0.0.3.0 compiler test.");
  fprintf(stderr,"[dynamic] game=%s\n[dynamic] objects=%s\n",as_utf8(prepared.game).c_str(),as_utf8(prepared.objects).c_str());
  dynamic_game_load(prepared.objects.wstring(),(prepared.game/L"code"/L"cking.rpx").wstring());
#endif
  arguments[2]=as_utf8(prepared.game);argv.clear();for(auto& arg:arguments)argv.push_back(arg.data());argv.push_back(nullptr);
#else
  if(testMode!="renderer-smoke"&&!std::filesystem::is_regular_file(game/L"code"/L"cking.rpx")){
   auto usb=find_usb_game();
   if(usb.empty())throw std::runtime_error("No extracted WWHD game found. Put your USA v0 code, content and meta folders in USB:\\WWHD, or LocalState/WWHD/game. Extract WUD/WUX/WUA images on the PC first.");
   arguments[2]=as_utf8(usb);argv.clear();for(auto& arg:arguments)argv.push_back(arg.data());argv.push_back(nullptr);
  }
#endif
  int result=0;
#ifdef WWHD_BUILTIN_GC_MINIMAP
  uwp_gc_minimap::configure();
#endif
  uwp_write_guard::run([&]{result=wwhd_game_main(int(arguments.size()),argv.data());});
  return result;
 }catch(std::exception const& e){fprintf(stderr,"[uwp startup] FAIL: %s\n",e.what());fflush(stderr);OutputDebugStringA(e.what());SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"WWHD UWP startup",e.what(),nullptr);return 1;}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){return SDL_RunApp(0,nullptr,run_game,nullptr);}
