// SPDX-License-Identifier: MPL-2.0
#include "uwp_texture_watch.h"
#ifdef WWHD_UWP
#include "runtime.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace uwp_texture_watch {
namespace {
uint8_t* base=nullptr;
uint64_t size=0,epoch=1;
size_t pageSize=0;
std::vector<uint64_t> stamps;
std::vector<void*> addresses;
std::mutex mutex;
std::atomic<bool> enabled=false;
int mode() {
 static const int value=[] {auto* e=std::getenv("WWHD_UWP_NATIVE_TEXTURE_WATCH");return e&&!std::strcmp(e,"verify")?2:e&&!std::strcmp(e,"optimized")?1:0;}();
 return value;
}
}
bool requested(){return mode()!=0;}
bool active(){return enabled.load(std::memory_order_acquire);}
bool verify(){return mode()==2;}
void init(uint8_t* nextBase,uint64_t nextSize) {
 if(!requested())return;
 SYSTEM_INFO info{};GetNativeSystemInfo(&info);
 base=nextBase;size=nextSize;pageSize=info.dwPageSize;
 if(!base || !pageSize || (pageSize&(pageSize-1)))return;
 stamps.assign((size+pageSize-1)/pageSize,0);
 enabled.store(true,std::memory_order_release);
 LOG("[native texture watch] %s; kernel write watch, exact page epochs; %.2f MiB stamp storage; buffer cache unchanged",verify()?"verify":"optimized",stamps.size()*sizeof(uint64_t)/1048576.0);
}
bool changed(const std::pair<uint32_t,uint32_t>* ranges,size_t count,uint64_t& stamp) {
 if(!active())return true;
 std::lock_guard<std::mutex> lock(mutex);
 if(!active())return true;
 const uint64_t old=stamp;
 bool dirty=!old;
 const uint64_t current=++epoch;
 for(size_t level=0;level<count;++level) {
  uint64_t begin=ranges[level].first,end=begin+ranges[level].second;
  if(!ranges[level].second || end>size){stamp=0;return true;}
  const uint64_t first=begin/pageSize,last=(end-1)/pageSize;
  const size_t pages=last-first+1;
  if(addresses.size()<pages)addresses.resize(pages);
  ULONG_PTR returned=pages;ULONG granularity=0;
  UINT error=GetWriteWatch(WRITE_WATCH_FLAG_RESET,base+first*pageSize,pages*pageSize,addresses.data(),&returned,&granularity);
  if(error || granularity!=pageSize || returned>pages) {
   enabled.store(false,std::memory_order_release);stamp=0;
   LOG("[native texture watch] query failed (%u, granularity %lu); full-byte fallback",error,granularity);
   return true;
  }
  for(size_t n=0;n<returned;++n) {
   auto offset=static_cast<uint8_t*>(addresses[n])-base;
   if(offset<0 || uint64_t(offset)>=size || size_t(offset)%pageSize) {
    enabled.store(false,std::memory_order_release);stamp=0;return true;
   }
   stamps[size_t(offset)/pageSize]=current;
  }
  for(uint64_t p=first;p<=last;++p)if(stamps[p]>old)dirty=true;
 }
 stamp=current;
 return dirty;
}
}
#else
namespace uwp_texture_watch {
bool requested(){return false;}void init(uint8_t*,uint64_t){}bool active(){return false;}bool verify(){return false;}
bool changed(const std::pair<uint32_t,uint32_t>*,size_t,uint64_t&){return true;}
}
#endif
