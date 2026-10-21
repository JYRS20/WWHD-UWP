// SPDX-License-Identifier: MPL-2.0
#include "preparation.h"
#include "preparation_ui.h"
#include "dynamic_game.h"
#include "../uwp-python/python_host.h"
#include "../uwp-extract/extract_api.h"
#include "../uwp-extract/uwp_files.h"
#include "../uwp-clang/compile_host.h"
#include <SDL3/SDL.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
namespace fs=std::filesystem;
fs::path preparation_log;
std::mutex report_mutex;
using namespace winrt::Windows::Storage;
enum class Action { Launch, Browse, Back };
struct Choice {
 Action action=Action::Launch;
 std::string label;
 StorageFolder folder{nullptr};
 StorageFolder drive{nullptr},game{nullptr},objects{nullptr};
 StorageFile image{nullptr},disc{nullptr},common{nullptr};
};
struct State {
 std::mutex mutex;
 std::vector<Choice> choices;
 std::string stage="Looking for USB drives...",detail,error,python_line,folder_path,display_game;
 bool browsing=false;
 std::atomic<int> percent{0};
 std::atomic<bool> finished{false},cancel{false};
 WwhdPreparedGame prepared;
 std::string test_mode;
 bool captured=false;
};
std::string read(const fs::path& path,size_t limit=1u<<20){
 wwhd_extract::InputFile file(path,std::ios::binary);
 if(!file)throw std::runtime_error("Cannot read preparation input. Check the USB drive.");
 file.seekg(0,std::ios::end);auto size=file.tellg();
 if(size<0||uint64_t(size)>limit)throw std::runtime_error("Preparation input exceeds its size limit.");
 file.seekg(0);std::string text(size_t(size),'\0');file.read(text.data(),size);
 if(file.gcount()!=size)throw std::runtime_error("USB input is incomplete or disconnected.");
 return text;
}
void write(const fs::path& path,const std::string& text){
 wwhd_extract::OutputFile file(path);if(!file)throw std::runtime_error("Cannot create preparation status on USB.");
 file.write(reinterpret_cast<const uint8_t*>(text.data()),text.size());
 if(!file.close())throw std::runtime_error("Cannot save preparation status. Check USB space and connection.");
}
std::string utf8(const fs::path& path){auto s=path.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::wstring lower(std::wstring text){for(auto& c:text)c=std::towlower(c);return text;}
void report(void*,const char* text);
void status(State& s,const char* stage,int percent,std::string detail={}){
 std::lock_guard lock(s.mutex);if(s.stage!=stage)report(nullptr,stage);
 s.stage=stage;s.percent=percent;s.detail=std::move(detail);
}
void report(void*,const char* text){
 std::lock_guard lock(report_mutex);
 fprintf(stderr,"[prepare] %s\n",text);fflush(stderr);
 // Close after each message so diagnostics remain readable while preparation is running.
 if(!preparation_log.empty()){std::ofstream out(preparation_log,std::ios::app);if(out)out<<text<<'\n';}
}
void check_cancel(State& s){if(s.cancel)throw std::runtime_error("Preparation cancelled. Completed preparations are preserved. Restart to try again.");}
bool extract_progress(void* p,uint64_t done,uint64_t total){
 auto& s=*static_cast<State*>(p);
 status(s,"Extracting game...",total?int(25.0*double(done)/double(total)):0,
        std::to_string(done>>20)+" / "+std::to_string(total>>20)+" MiB");
 return !s.cancel;
}
bool compile_progress(void* p,unsigned done,unsigned total){
 auto& s=*static_cast<State*>(p);
 status(s,"Compiling game code...",35+int(60*done/total),std::to_string(done)+" / "+std::to_string(total)+" files");
 return !s.cancel;
}
void python_report(void* p,const char* bytes,size_t size){
 auto& s=*static_cast<State*>(p);
 // CPython can split print calls across callbacks; assemble complete lines.
 s.python_line.append(bytes,size);
 size_t end;
 while((end=s.python_line.find('\n'))!=std::string::npos){
  auto line=s.python_line.substr(0,end);s.python_line.erase(0,end+1);
  report(nullptr,line.c_str());unsigned unit=0;
  if(std::sscanf(line.c_str(),"GENERATING code_%u.c",&unit)==1)
   status(s,"Translating game code...",25+int(10*(std::min)(unit,77u)/78),std::to_string(unit+1)+" / 78 files");
 }
}
void fail(State& s,const std::string& error){
 report(nullptr,error.c_str());{std::lock_guard lock(s.mutex);s.error=error;}s.finished=true;
}
void add_images(std::vector<Choice>& choices,const StorageFolder& drive,const StorageFolder& folder){
 std::vector<StorageFile> images,keys;StorageFile common{nullptr};
 for(auto const& file:folder.GetFilesAsync().get()){
  auto ext=lower(file.FileType().c_str());
  if(ext==L".wux"||ext==L".wud")images.push_back(file);
  if(lower(file.Name().c_str())==L"common.key")common=file;
  else if(ext==L".key")keys.push_back(file);
 }
 for(auto const& image:images){
  Choice c;c.drive=drive;c.image=image;c.common=common;
  auto expected=lower(image.DisplayName().c_str())+L".key";
  for(auto const& key:keys)if(lower(key.Name().c_str())==expected)c.disc=key;
  if(!c.disc&&keys.size()==1)c.disc=keys.front();
  c.label="Prepare - "+winrt::to_string(image.Name());
  if(!c.common||!c.disc)c.label+=" (keys missing or ambiguous)";
  choices.push_back(std::move(c));
 }
}
void scan(State& state,StorageFolder browse_drive=nullptr,StorageFolder browse_folder=nullptr){
 winrt::init_apartment(winrt::apartment_type::multi_threaded);
 try{
  std::vector<StorageFolder> drives;
  try{auto f=ApplicationData::Current().LocalFolder().GetFileAsync(L"preparation-test-mode.txt").get();auto mode=winrt::to_string(FileIO::ReadTextAsync(f).get());
      std::lock_guard lock(state.mutex);state.test_mode=std::move(mode);
      while(!state.test_mode.empty()&&(state.test_mode.back()=='\r'||state.test_mode.back()=='\n'))state.test_mode.pop_back();}catch(winrt::hresult_error const&){}
  // Private validation override is never packaged or created for normal users.
  try{auto config=ApplicationData::Current().LocalFolder().GetFileAsync(L"preparation-test-root.txt").get();
      drives.push_back(StorageFolder::GetFolderFromPathAsync(FileIO::ReadTextAsync(config).get()).get());}catch(winrt::hresult_error const&){}
  if(browse_folder){
   std::vector<Choice> choices;
   Choice back;back.action=Action::Back;back.label="[..] Back";choices.push_back(std::move(back));
   std::string error;
   try{
    for(auto const& folder:browse_folder.GetFoldersAsync().get()){
     Choice c;c.action=Action::Browse;c.drive=browse_drive;c.folder=folder;
     c.label="[Folder] "+winrt::to_string(folder.Name());choices.push_back(std::move(c));
    }
    add_images(choices,browse_drive,browse_folder);
   }catch(winrt::hresult_error const& e){error="Cannot read this folder. Press B to go back. "+winrt::to_string(e.message());}
   {
    std::lock_guard lock(state.mutex);state.choices=std::move(choices);state.stage="Browse USB";state.browsing=true;state.folder_path=winrt::to_string(browse_folder.Path());
    auto path=winrt::to_string(browse_folder.Path());if(path.size()>128)path="..."+path.substr(path.size()-125);
    state.detail=error.empty()?path:error;
   }
   report(nullptr,("Browsing: "+winrt::to_string(browse_folder.Path())).c_str());state.finished=true;
   winrt::uninit_apartment();return;
  }
  for(auto const& d:KnownFolders::RemovableDevices().GetFoldersAsync().get())drives.push_back(d);
  auto home=fs::path(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str());
  auto profile=read(home/L"preparation-profile.txt");
  std::vector<Choice> choices;
  for(auto const& drive:drives){
   check_cancel(state);
   // Always expose the drive, even when its root contains no game image.
   Choice browse;browse.action=Action::Browse;browse.drive=drive;browse.folder=drive;
   browse.label="Browse USB - "+winrt::to_string(drive.Name());choices.push_back(std::move(browse));
   try{
    auto prep=drive.GetFolderAsync(L"WWHD-UWP-Preparation").get();
    for(auto const& job:prep.GetFoldersAsync().get()){
     if(std::wstring(job.Name()).rfind(L"import",0)!=0)continue;
     try{
      auto game=job.GetFolderAsync(L"game").get();(void)game.GetFolderAsync(L"code").get().GetFileAsync(L"cking.rpx").get();
      (void)game.GetFolderAsync(L"content").get();(void)game.GetFolderAsync(L"meta").get();
      // A new pipeline profile invalidates only unified preparations; v3 objects are checked by the same signed hash profile.
      bool unified=std::wstring(job.Name()).rfind(L"import-unified",0)==0;
      if(unified){auto marker=job.GetFileAsync(L"ready-profile.txt").get();if(read(fs::path(marker.Path().c_str()))!=profile)continue;}
      auto generated=job.GetFolderAsync(L"generated").get();
      for(auto const& objects:generated.GetFoldersAsync().get()){
       if(std::wstring(objects.Name()).rfind(L"objects",0)!=0)continue;
       try{(void)objects.GetFileAsync(L"compilation-complete.json").get();}catch(winrt::hresult_error const&){continue;}
       Choice c;c.drive=drive;c.game=game;c.objects=objects;
       c.label="Play prepared game - "+winrt::to_string(drive.Name())+" / "+winrt::to_string(job.Name());choices.push_back(std::move(c));break;
      }
     }catch(winrt::hresult_error const&){}
    }
   }catch(winrt::hresult_error const&){}
   std::vector<StorageFolder> folders{drive};
   try{folders.push_back(drive.GetFolderAsync(L"WWHD").get());}catch(winrt::hresult_error const&){}
   for(auto const& folder:folders){
    try{add_images(choices,drive,folder);}catch(winrt::hresult_error const&){}
   }
  }
  // Prefer current completed preparations over legacy diagnostics or a new extraction.
  auto rank=[](const Choice& c){return !c.game?2:winrt::to_string(c.game.Path()).find("import-unified")!=std::string::npos?0:1;};
  std::stable_sort(choices.begin(),choices.end(),[&](const Choice& a,const Choice& b){return rank(a)<rank(b);});
  {std::lock_guard lock(state.mutex);state.choices=std::move(choices);state.stage="Select your game";state.browsing=false;state.folder_path.clear();
   state.detail=state.choices.empty()?"Connect a USB drive and press X to rescan.":"Choose a prepared game, an image, or Browse USB to open a folder.";}
  state.finished=true;
 }catch(winrt::hresult_error const& e){fail(state,"USB access failed: "+winrt::to_string(e.message()));}
  catch(std::exception const& e){fail(state,e.what());}
 winrt::uninit_apartment();
}
void prepare(State& s,Choice choice){
 winrt::init_apartment(winrt::apartment_type::multi_threaded);
 try{
  auto home=fs::path(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str());
  auto profile=read(home/L"preparation-profile.txt");StorageFolder job{nullptr};
  {std::lock_guard lock(s.mutex);s.display_game=choice.image?winrt::to_string(choice.image.Name()):"The Wind Waker HD";}
  if(!choice.game){
   if(!choice.common||!choice.disc)throw std::runtime_error("Place common.key and the matching disc .key next to the image. With multiple disc keys, use the image filename for its key.");
   check_cancel(s);
   status(s,"Checking USB space...",0);
   // Reserve headroom for ~1.8 GiB extracted assets, translated source and compiler output.
   uint64_t free=0;
   try{auto values=choice.drive.Properties().RetrievePropertiesAsync({L"System.FreeSpace"}).get();
       free=winrt::unbox_value<uint64_t>(values.Lookup(L"System.FreeSpace"));}catch(winrt::hresult_error const&){report(nullptr,"USB free-space query unavailable; write failures will stop preparation.");}
   if(free&&free<(uint64_t(3)<<30))throw std::runtime_error("At least 3 GiB of free USB space is required for a new preparation. Existing completed games can still be played.");
   auto prep=choice.drive.CreateFolderAsync(L"WWHD-UWP-Preparation",CreationCollisionOption::OpenIfExists).get();
   job=prep.CreateFolderAsync(L"import-unified",CreationCollisionOption::GenerateUniqueName).get();
   choice.game=job.CreateFolderAsync(L"game",CreationCollisionOption::FailIfExists).get();
   auto generated=job.CreateFolderAsync(L"generated",CreationCollisionOption::FailIfExists).get();
   auto image=winrt::to_string(choice.image.Path()),disc=winrt::to_string(choice.disc.Path()),common=winrt::to_string(choice.common.Path()),out=winrt::to_string(choice.game.Path());
   report(nullptr,("Selected image: "+winrt::to_string(choice.image.Name())).c_str());
   status(s,"Extracting game...",0);
   int extracted=wwhd_extract::extract_disc(image.c_str(),disc.c_str(),common.c_str(),out.c_str(),&s,extract_progress,report);
   check_cancel(s);if(extracted)throw std::runtime_error("Disc extraction failed (code "+std::to_string(extracted)+"). Check the image, keys and USB space. See wwhd-console.log.");
   status(s,"Translating game code...",25);
   auto rpx=fs::path(choice.game.Path().c_str())/L"code"/L"cking.rpx";
   auto script=read(home/L"prepare_game.py");auto tools=home/L"Tools";
   if(wwhd_python_run(home.c_str(),tools.c_str(),generated.Path().c_str(),rpx.c_str(),script.c_str(),&s,python_report))
    throw std::runtime_error("Game translation failed. Only the USA original release is supported. See wwhd-console.log.");
   check_cancel(s);
   choice.objects=generated.CreateFolderAsync(L"objects",CreationCollisionOption::FailIfExists).get();
   auto headers=home/L"CompilerHeaders";
   if(wwhd_compile::compile_generated(headers.c_str(),generated.Path().c_str(),choice.objects.Path().c_str(),&s,report,compile_progress)){
    check_cancel(s);throw std::runtime_error("Game compilation failed. Check USB connection and free space. See wwhd-console.log.");
   }
  }
  check_cancel(s);
  status(s,"Preparing to launch...",95,"Checking game and all 80 compiled files");
  winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().AddOrReplace(L"wwhd-unified-game",choice.game);
  auto game=fs::path(choice.game.Path().c_str()),objects=fs::path(choice.objects.Path().c_str());
  report(nullptr,("Game="+utf8(game)+"; objects="+utf8(objects)).c_str());
  dynamic_game_load(objects.wstring(),(game/L"code"/L"cking.rpx").wstring());
  // Written last: cancelled, incomplete or incompatible jobs are never selected as ready.
  if(job)write(fs::path(job.Path().c_str())/L"ready-profile.txt",profile);
  s.prepared={game,objects};status(s,"Ready",100,"Starting game...");s.finished=true;
 }catch(winrt::hresult_error const& e){fail(s,"Preparation failed: "+winrt::to_string(e.message()));}
  catch(std::exception const& e){fail(s,e.what());}
 winrt::uninit_apartment();
}
struct Screen {
 SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;std::vector<SDL_Gamepad*> pads;
 std::unique_ptr<wwhd_menu::Renderer> menu;
 Screen(){
  if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))throw std::runtime_error(SDL_GetError());
  window=SDL_CreateWindow("WWHD UWP",1920,1080,SDL_WINDOW_FULLSCREEN);
  if(window)renderer=SDL_CreateRenderer(window,"direct3d11");
  if(!renderer){std::string error=SDL_GetError();if(window)SDL_DestroyWindow(window);SDL_Quit();throw std::runtime_error("Cannot create preparation screen: "+error);}
  SDL_SetRenderVSync(renderer,1);
  SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX);
  try{auto home=fs::path(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str());menu=std::make_unique<wwhd_menu::Renderer>(renderer,home/L"Assets"/L"Menu");}
  catch(...){SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();throw;}
  int width=0,height=0;SDL_GetRenderOutputSize(renderer,&width,&height);
  report(nullptr,("Menu renderer: "+std::string(SDL_GetRendererName(renderer))+"; output="+std::to_string(width)+"x"+std::to_string(height)).c_str());
  int count=0;auto ids=SDL_GetGamepads(&count);for(int i=0;i<count;i++)open(ids[i]);SDL_free(ids);
 }
 void open(SDL_JoystickID id){for(auto p:pads)if(SDL_GetGamepadID(p)==id)return;if(auto p=SDL_OpenGamepad(id))pads.push_back(p);}
 ~Screen(){menu.reset();for(auto p:pads)SDL_CloseGamepad(p);if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);SDL_Quit();}
 void frame(State& s,bool running,size_t selected_index){
  std::lock_guard lock(s.mutex);
  wwhd_menu::View view;view.browsing=s.browsing;view.loading=!s.finished&&!running;view.running=running;view.cancelling=s.cancel;
  for(char c:s.folder_path){if(c=='\\')view.folder+=" / ";else view.folder+=c;}
  view.game=s.display_game;view.stage=s.stage;view.detail=s.detail;view.error=s.error;view.percent=unsigned(s.percent.load());view.selected=selected_index;
  const Choice* keys=nullptr;
  for(size_t i=0;i<s.choices.size();++i){
   const auto& c=s.choices[i];wwhd_menu::Row row;view.connected|=bool(c.drive);
   if(c.game){row.title="Play game";row.subtitle="The Wind Waker HD · USA";row.icon=wwhd_menu::Icon::Play;row.ready=true;row.tag="READY";}
   else if(c.action==Action::Back){auto parent_name=utf8(fs::u8path(s.folder_path).parent_path().filename());row.title=parent_name.empty()?"Back to menu":"Back to "+parent_name;row.icon=wwhd_menu::Icon::Folder;row.tag="PARENT FOLDER";}
   else if(c.action==Action::Browse){row.title=s.browsing?winrt::to_string(c.folder.Name()):"Browse USB";row.subtitle=winrt::to_string(c.drive.Name())+" · Select a game image from any folder.";row.icon=wwhd_menu::Icon::Folder;row.tag="FOLDER";}
   else if(c.image){row.title=s.browsing?winrt::to_string(c.image.Name()):"Prepare game";row.subtitle=winrt::to_string(c.image.Name());row.icon=wwhd_menu::Icon::Disc;row.tag=c.common&&c.disc?"GAME IMAGE":"CHECK KEYS";if(!keys||i==selected_index)keys=&c;}
   else{row.title=c.label;}
   view.rows.push_back(std::move(row));
  }
  if(view.stage=="Compiling game code..."){unsigned done=0,total=0;if(std::sscanf(view.detail.c_str(),"%u / %u files",&done,&total)==2)view.detail=std::to_string(done)+" of "+std::to_string(total)+" files compiled";}
  if(keys){view.keys_visible=true;view.keys_ready=bool(keys->disc)&&bool(keys->common);}
  // Isolated visual fixtures are enabled only by a non-packaged LocalState test file.
  if(s.test_mode.rfind("ui-",0)==0){
   view={};view.connected=true;view.rows={{"Play game","The Wind Waker HD · USA","READY",wwhd_menu::Icon::Play,true},{"Browse USB","Select a game image from any folder.","",wwhd_menu::Icon::Folder,false}};
   if(s.test_mode=="ui-browser"){view.browsing=true;view.folder="E:   /   Games   /   The Wind Waker HD";view.selected=1;view.rows={{"Back to Games","","PARENT FOLDER",wwhd_menu::Icon::Folder,false},{"The Wind Waker HD (USA).wux","","GAME IMAGE",wwhd_menu::Icon::Disc,false}};view.keys_visible=true;view.keys_ready=true;}
   if(s.test_mode=="ui-progress"){view.running=true;view.game="The Wind Waker HD (USA).wux";view.stage="Compiling game code...";view.detail="36 of 80 files compiled";view.percent=62;}
   if(s.test_mode=="ui-error")view.error="Place common.key and the matching disc .key next to the image. With multiple disc keys, use the image filename for its key.";
   if(s.test_mode=="ui-empty"){view.connected=false;view.rows.clear();}
  }
  SDL_Texture* proof=nullptr;
  if(s.test_mode.rfind("ui-",0)==0){
   proof=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,1920,1080);
   if(proof)SDL_SetRenderTarget(renderer,proof);
  }
  SDL_SetRenderViewport(renderer,nullptr);
  menu->draw(view);
  if(!s.test_mode.empty()&&!s.captured&&s.finished&&!running){
   if(auto pixels=SDL_RenderReadPixels(renderer,nullptr)){
    auto path=fs::path(ApplicationData::Current().LocalFolder().Path().c_str())/L"WWHD"/L"preparation-screen.bmp";
    std::vector<uint8_t> bytes(size_t(pixels->w)*size_t(pixels->h)*4+4096);
    if(auto stream=SDL_IOFromMem(bytes.data(),bytes.size())){
     if(SDL_SaveBMP_IO(pixels,stream,false)){auto length=SDL_TellIO(stream);if(length>0)write(path,std::string(reinterpret_cast<const char*>(bytes.data()),size_t(length)));}
     SDL_CloseIO(stream);
    }SDL_DestroySurface(pixels);
   }s.captured=true;
  }
  if(proof){SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(proof);}
  SDL_RenderPresent(renderer);
 }
};
}
WwhdPreparedGame wwhd_prepare_and_load(){
 preparation_log=fs::current_path()/L"wwhd-prepare.log";write(preparation_log,"WWHD UWP unified preparation 0.1.2\n");
 Screen screen;State state;size_t selected=0;bool running=false,quit=false;
 StorageFolder browse_drive{nullptr},browse_folder{nullptr};std::vector<StorageFolder> parents;
 std::thread worker([&]{scan(state);});
 auto start_scan=[&]{state.finished=false;state.error.clear();state.captured=false;selected=0;
  auto drive=browse_drive,folder=browse_folder;worker=std::thread([&,drive,folder]{scan(state,drive,folder);});
 };
 auto parent=[&]{if(parents.empty()){browse_folder=nullptr;browse_drive=nullptr;}else{browse_folder=parents.back();parents.pop_back();}start_scan();};
 try{
  for(;;){
   SDL_Event event;bool accept=false,rescan=false,back=false;int move=0;
   while(SDL_PollEvent(&event)){
    if(event.type==SDL_EVENT_GAMEPAD_ADDED)screen.open(event.gdevice.which);
    if(event.type==SDL_EVENT_QUIT)quit=true;
    if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat){
     if(event.key.key==SDLK_RETURN)accept=true;
     if(event.key.key==SDLK_UP)move=-1;if(event.key.key==SDLK_DOWN)move=1;
     if(event.key.key==SDLK_X)rescan=true;
     if(event.key.key==SDLK_ESCAPE){back=true;}
    }
    if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){
     if(event.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH)accept=true;
     if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_UP)move=-1;
     if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_DOWN)move=1;
     if(event.gbutton.button==SDL_GAMEPAD_BUTTON_WEST)rescan=true;
     if(event.gbutton.button==SDL_GAMEPAD_BUTTON_EAST){back=true;}
    }
   }
   if(back){
    if(running){if(state.finished)quit=true;else if(state.percent<95)state.cancel=true;}
    else if(state.finished){if(worker.joinable())worker.join();if(browse_folder)parent();else quit=true;}
   }
   if(quit)state.cancel=true;
   if(state.finished&&worker.joinable())worker.join();
   if(running&&state.finished){
    screen.frame(state,true,selected);
    if(!state.prepared.game.empty()&&!quit){SDL_Delay(250);return state.prepared;}
    if(quit)break;
    // Python is isolated to one run per process; failures require restart, not a partial retry.
    if(accept||rescan)quit=true;
   }
   if(quit&&state.finished)break;
   if(!running&&state.finished){
    if(!state.test_mode.empty()&&state.error.empty()){
     screen.frame(state,false,selected);
     if(state.test_mode.rfind("ui-",0)==0){quit=true;}
     else if(state.test_mode=="browse"){
      bool found=false;
      for(size_t i=0;i<state.choices.size();++i)if(state.choices[i].action==Action::Browse){selected=i;accept=true;found=true;break;}
      if(!found){
       for(auto const& c:state.choices)if(c.image&&c.disc&&c.common){report(nullptr,"PASS browser: nested image and both keys discovered");found=true;break;}
       if(!found)report(nullptr,"FAIL browser: nested inputs not found");quit=true;
      }
     }else for(size_t i=0;i<state.choices.size();++i){
      bool match=state.test_mode=="prepared"?bool(state.choices[i].game):state.test_mode=="prepare"&&bool(state.choices[i].image);
      if(match){selected=i;accept=true;break;}
     }
     if(state.test_mode!="browse")state.test_mode.clear();
    }
    if(move&&!state.choices.empty())selected=(selected+state.choices.size()+move)%state.choices.size();
    if(rescan)start_scan();
    else if(accept&&!state.choices.empty()&&state.error.empty()){
     auto choice=state.choices[selected];
     if(choice.action==Action::Back)parent();
     else if(choice.action==Action::Browse){
      if(browse_folder)parents.push_back(browse_folder);
      browse_drive=choice.drive;browse_folder=choice.folder;start_scan();
     }else{state.finished=false;running=true;worker=std::thread([&,choice]{prepare(state,choice);});}
    }
   }
   screen.frame(state,running,selected);SDL_Delay(10);
  }
 }catch(...){state.cancel=true;if(worker.joinable())worker.join();throw;}
 if(worker.joinable())worker.join();return {};
}
