// SPDX-License-Identifier: MPL-2.0
#ifdef WWHD_UWP
#include "uwp_browser.h"
#include "file_access.h"
#include "overlay/hostui.h"
#include "imgui.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace uwp_browser {
namespace {
using namespace winrt::Windows::Storage;
namespace fs=std::filesystem;
struct Item {std::string name;StorageFolder folder{nullptr};StorageFile file{nullptr};};
struct Listing {std::vector<Item> items;std::string path,error;};
struct Request {bool folder;std::function<void(std::string)> chosen;};
std::mutex request_mutex;
std::unique_ptr<Request> pending;
std::atomic<bool> visible{false};
struct Browser {
 std::unique_ptr<Request> request;
 StorageFolder folder{nullptr};std::vector<StorageFolder> parents;
 Listing list;std::future<Listing> scan;
 std::future<std::string> importing;
 bool popup=false,focus=false;
 std::shared_ptr<std::atomic<uint64_t>> copied;
};
Browser browser;
std::string utf8(const fs::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::wstring lower(std::wstring s){for(auto& c:s)c=std::towlower(c);return s;}
Listing list_folder(StorageFolder folder,bool select_folder){
 winrt::init_apartment(winrt::apartment_type::multi_threaded);
 Listing result;
 try{
  if(!folder){
   auto app=ApplicationData::Current().LocalFolder().CreateFolderAsync(L"WWHD",CreationCollisionOption::OpenIfExists).get();
   auto local=app.CreateFolderAsync(L"ModPackages",CreationCollisionOption::OpenIfExists).get();
   result.items.push_back({"Internal mod packages",local});
   try{for(auto const& drive:KnownFolders::RemovableDevices().GetFoldersAsync().get())result.items.push_back({"USB - "+winrt::to_string(drive.Name()),drive});}
   catch(winrt::hresult_error const&){result.error="USB unavailable. You can still use internal mod packages.";}
   result.path="Internal packages: LocalState / WWHD / ModPackages";
  }else{
   result.path=winrt::to_string(folder.Path());
   for(auto const& child:folder.GetFoldersAsync().get())result.items.push_back({winrt::to_string(child.Name()),child});
   if(!select_folder)for(auto const& file:folder.GetFilesAsync().get()){
    auto ext=lower(std::wstring(file.FileType()));
    if(ext==L".zip"||ext==L".wwhdmod"||ext==L".pack")result.items.push_back({winrt::to_string(file.Name()),nullptr,file});
   }
   std::stable_sort(result.items.begin(),result.items.end(),[](const Item& a,const Item& b){if(bool(a.folder)!=bool(b.folder))return bool(a.folder);return a.name<b.name;});
  }
 }catch(winrt::hresult_error const& e){result.error="Cannot read this folder: "+winrt::to_string(e.message());}
 winrt::uninit_apartment();return result;
}
void scan(){
 browser.list={};browser.focus=true;
 auto folder=browser.folder;auto mode=browser.request->folder;
 browser.scan=std::async(std::launch::async,[folder,mode]{return list_folder(folder,mode);});
}
struct CopyBudget {uint64_t bytes=0;unsigned entries=0;std::shared_ptr<std::atomic<uint64_t>> progress;};
void copy_file(const fs::path& from,const fs::path& to,CopyBudget& budget){
 auto handle=host::create_file(from.c_str(),GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL);
 if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot read the selected mod. Check the storage connection.");
 struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{handle};
 LARGE_INTEGER size{};
 if(!GetFileSizeEx(handle,&size)||size.QuadPart<0||uint64_t(size.QuadPart)>(512ull<<20)||budget.bytes+uint64_t(size.QuadPart)>(512ull<<20))throw std::runtime_error("The mod exceeds the 512 MiB import limit.");
 fs::create_directories(to.parent_path());std::ofstream out(to,std::ios::binary|std::ios::trunc);
 if(!out)throw std::runtime_error("Cannot create the mod import. Check internal storage space.");
 std::vector<char> data(1u<<20);uint64_t read=0;
 for(;;){DWORD count=0;if(!ReadFile(handle,data.data(),DWORD(data.size()),&count,nullptr))throw std::runtime_error("The mod read was interrupted.");if(!count)break;
  read+=count;budget.bytes+=count;if(budget.bytes>(512ull<<20))throw std::runtime_error("The mod exceeds the 512 MiB import limit.");
  out.write(data.data(),count);if(!out)throw std::runtime_error("Cannot write the mod import. Check internal storage space.");budget.progress->store(budget.bytes);
 }
 out.close();if(!out||read!=uint64_t(size.QuadPart))throw std::runtime_error("The selected mod is incomplete or changed during import.");
}
void copy_folder(const fs::path& from,const fs::path& to,CopyBudget& budget,unsigned depth=0){
 if(depth>64)throw std::runtime_error("The mod folder is nested too deeply.");
 fs::create_directories(to);WIN32_FIND_DATAW data{};
 auto search=FindFirstFileExFromAppW((from/L"*").c_str(),FindExInfoStandard,&data,FindExSearchNameMatch,nullptr,0);
 if(search==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return;throw std::runtime_error("Cannot read the complete mod folder.");}
 struct Close {HANDLE h;~Close(){FindClose(h);}} close{search};
 do{
  std::wstring name=data.cFileName;if(name==L"."||name==L"..")continue;
  if(++budget.entries>4096)throw std::runtime_error("The mod has too many files.");
  if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Mod folders may not contain links.");
  if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)copy_folder(from/name,to/name,budget,depth+1);else copy_file(from/name,to/name,budget);
 }while(FindNextFileW(search,&data));
 if(GetLastError()!=ERROR_NO_MORE_FILES)throw std::runtime_error("The mod folder read was interrupted.");
}
std::string stage(StorageFolder folder,StorageFile file,std::shared_ptr<std::atomic<uint64_t>> progress){
 winrt::init_apartment(winrt::apartment_type::multi_threaded);
 try{
  using winrt::Windows::Storage::AccessCache::StorageApplicationPermissions;
  if(folder)StorageApplicationPermissions::FutureAccessList().AddOrReplace(L"wwhd-mod-source",folder);
  else StorageApplicationPermissions::FutureAccessList().AddOrReplace(L"wwhd-mod-source",file);
  auto source=fs::path(folder?folder.Path().c_str():file.Path().c_str());
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  if(!GetFileAttributesExFromAppW(source.c_str(),GetFileExInfoStandard,&attributes)||attributes.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Invalid mod source or symbolic link.");
  auto app=fs::path(ApplicationData::Current().TemporaryFolder().Path().c_str())/L"WWHD-ModImports";
  fs::create_directories(app);auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path work;
  do{work=app/std::to_wstring(stamp++);}while(!fs::create_directory(work));
  auto dest=work/source.filename();CopyBudget budget;budget.progress=progress;
  if(folder)copy_folder(source,dest,budget);else copy_file(source,dest,budget);
  auto result=utf8(dest);winrt::uninit_apartment();return result;
 }catch(...){winrt::uninit_apartment();throw;}
}
void select(StorageFolder folder,StorageFile file){
 browser.copied=std::make_shared<std::atomic<uint64_t>>(0);auto progress=browser.copied;
 browser.importing=std::async(std::launch::async,[folder,file,progress]{return stage(folder,file,progress);});
}
}
void choose_mod(bool folder,std::function<void(std::string)> chosen){
 std::lock_guard lock(request_mutex);if(visible.load()||pending)return;
 pending=std::make_unique<Request>(Request{folder,std::move(chosen)});visible=true;
}
bool active(){return visible.load();}
void draw(){
 {std::lock_guard lock(request_mutex);if(pending){browser.request=std::move(pending);browser.folder=nullptr;browser.parents.clear();browser.popup=true;scan();}}
 if(!browser.request)return;
 if(browser.popup){ImGui::OpenPopup("Choose mod source");browser.popup=false;}
 auto size=ImGui::GetIO().DisplaySize;
 ImGui::SetNextWindowSize(ImVec2((std::min)(size.x-32,850.f),(std::min)(size.y-32,560.f)),ImGuiCond_Appearing);
 if(!ImGui::BeginPopupModal("Choose mod source",nullptr,ImGuiWindowFlags_NoSavedSettings))return;
 bool busy=browser.scan.valid()||browser.importing.valid();
 if(browser.scan.valid()&&browser.scan.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  try{browser.list=browser.scan.get();browser.focus=true;}catch(winrt::hresult_error const& e){browser.list.error=winrt::to_string(e.message());}catch(std::exception const& e){browser.list.error=e.what();}busy=false;
 }
 bool close=false;
 if(browser.importing.valid()&&browser.importing.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  try{auto path=browser.importing.get();auto chosen=browser.request->chosen;hostui::post([chosen,path]{chosen(path);});close=true;}
  catch(winrt::hresult_error const& e){browser.list.error=winrt::to_string(e.message());}
  catch(std::exception const& e){browser.list.error=e.what();}busy=false;
 }
 ImGui::TextWrapped("%s",browser.list.path.c_str());
 ImGui::TextDisabled("%s",browser.request->folder?"Open your mod folder, then choose Use this folder.":"Select a .wwhdmod, ZIP or replacement .pack file.");
 if(!browser.list.error.empty())ImGui::TextWrapped("%s",browser.list.error.c_str());
 if(browser.importing.valid())ImGui::Text("Importing mod: %.1f MiB",double(browser.copied->load())/(1u<<20));
 else if(busy)ImGui::TextUnformatted("Reading storage...");
 ImGui::BeginDisabled(busy);
 if(ImGui::Button("Storage devices")){browser.folder=nullptr;browser.parents.clear();scan();}
 ImGui::SameLine();
 if(ImGui::Button("Back")){
  if(browser.parents.empty())browser.folder=nullptr;else{browser.folder=browser.parents.back();browser.parents.pop_back();}scan();
 }
 ImGui::SameLine();if(ImGui::Button("Refresh"))scan();
 if(browser.request->folder&&browser.folder){ImGui::SameLine();if(ImGui::Button("Use this folder"))select(browser.folder,nullptr);}
 ImGui::BeginChild("##mod_sources",ImVec2(0,-45),ImGuiChildFlags_Borders);
 for(size_t i=0;i<browser.list.items.size();++i){const auto item=browser.list.items[i];ImGui::PushID(int(i));
  if(browser.focus&&i==0)ImGui::SetKeyboardFocusHere();
  auto label=(item.folder?"[Folder] ":"[File] ")+item.name;
  if(ImGui::Selectable(label.c_str(),false)){
   if(item.folder){if(browser.folder)browser.parents.push_back(browser.folder);browser.folder=item.folder;scan();}
   else select(nullptr,item.file);
   ImGui::PopID();break;
  }ImGui::PopID();
 }
 browser.focus=false;ImGui::EndChild();
 if(ImGui::Button("Cancel")||(!busy&&(ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)||ImGui::IsKeyPressed(ImGuiKey_Escape,false))))close=true;
 ImGui::EndDisabled();
 if(close&&!browser.scan.valid()&&!browser.importing.valid()){ImGui::CloseCurrentPopup();browser.request.reset();visible=false;}
 ImGui::EndPopup();
}
}
#endif
