// SPDX-License-Identifier: MPL-2.0
// Explicit renderer-smoke diagnostic only; never changes guest allocation.
#ifdef WWHD_UWP
#include "backend.h"
#include "runtime.h"
#include "uwp_texture_hash.h"
#include "../../exception_report.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#define XXH_INLINE_ALL
#include "../../../third_party/xxhash/xxhash.h"

namespace gfxvk {
void native_write_watch_probe() {
 constexpr size_t size=64ull<<20;
 struct Allocation {void* p=nullptr;~Allocation(){if(p)VirtualFree(p,0,MEM_RELEASE);}} allocation;
 allocation.p=VirtualAllocFromApp(nullptr,size,MEM_RESERVE|MEM_COMMIT|MEM_WRITE_WATCH,PAGE_READWRITE);
 if(!allocation.p) {LOG("[native write watch probe] unavailable: allocation error %lu; no game change",GetLastError());return;}
 auto* bytes=static_cast<uint8_t*>(allocation.p);
 SYSTEM_INFO info{};GetNativeSystemInfo(&info);const size_t page=info.dwPageSize;
 std::vector<void*> addresses(size/page);
 auto query=[&](size_t offset,size_t length,bool reset) {
  ULONG_PTR count=addresses.size();ULONG granularity=0;
  UINT result=GetWriteWatch(reset?WRITE_WATCH_FLAG_RESET:0,bytes+offset,length,addresses.data(),&count,&granularity);
  if(result || granularity!=page)exception_report::raise("native write watch query failed");
  return size_t(count);
 };
 std::memset(bytes,0x37,size);query(0,size,true);
 if(query(0,size,false)!=0)exception_report::raise("native write watch reset failed");
 bytes[1]=3;bytes[page*3+7]=9;bytes[size-1]=2;
 if(query(0,size,false)!=3 || query(0,size,true)!=3 || query(0,size,false)!=0)
  exception_report::raise("native write watch sparse/tail/reset failed");
 std::thread writer([&]{std::memset(bytes+page*8+17,0x29,page*2);});writer.join();
 if(query(page*8,page*3,false)!=3 || query(page*8,page,true)!=1 || query(0,size,true)!=2)
  exception_report::raise("native write watch cross-thread/overlap failed");
 // Make writes race with atomic retrieve+reset. A final query must account for
 // every written page across the union of all returns (never clear separately).
 constexpr size_t racingPages=256;
 std::array<bool,racingPages> seen{};
 std::atomic<bool> done=false;
 std::thread racing([&]{for(size_t n=0;n<racingPages;++n)bytes[n*page]^=1;done.store(true,std::memory_order_release);});
 auto collect=[&]{auto count=query(0,size,true);for(size_t n=0;n<count;++n){auto p=(static_cast<uint8_t*>(addresses[n])-bytes)/page;if(p<racingPages)seen[p]=true;}};
 do{collect();}while(!done.load(std::memory_order_acquire));racing.join();collect();
 if(!std::all_of(seen.begin(),seen.end(),[](bool v){return v;}))exception_report::raise("native write watch lost concurrent writes");
 using Clock=std::chrono::steady_clock;
 auto measure=[&](auto fn,int iterations){auto start=Clock::now();for(int n=0;n<iterations;++n)fn(n);return std::chrono::duration<double,std::milli>(Clock::now()-start).count()/iterations;};
 volatile uint64_t sink=0;
 const double fullHash=measure([&](int){sink=XXH3_64bits(bytes,size);},64);
 const double avxHash=SDL_HasAVX2()?measure([&](int){sink=uwp_texture_hash_avx2(bytes,size);},64):-1;
 const double cleanWhole=measure([&](int){sink=query(0,size,true);},512);
 const double cleanMany=measure([&](int){for(size_t n=0;n<128;++n)sink=query(n*(size/128),size/128,true);},32);
 const double dirtyWhole=measure([&](int){for(size_t n=0;n<16;++n)bytes[n*page]^=1;sink=query(0,size,true);},128);
 LOG("[native write watch probe] PASS sparse/tail/reset/overlap/cross-thread/concurrent writes; page %zu",page);
 LOG("[native write watch probe] 64MiB: full XXH3 %.4f ms; current AVX2 %.4f ms; clean one query %.4f ms; 128 clean subrange queries %.4f ms; 16 dirty pages one query %.4f ms; component benchmark NOT game FPS",fullHash,avxHash,cleanWhole,cleanMany,dirtyWhole);
 (void)sink;
 // Guest memory uses a 4 GiB reservation with lazy commit. Confirm that allocation
 // pattern separately; never commit 4 GiB or disturb the game's address space.
 Allocation reservation;reservation.p=VirtualAllocFromApp(nullptr,0x100000000ull,MEM_RESERVE|MEM_WRITE_WATCH,PAGE_READWRITE);
 if(!reservation.p)exception_report::raise("native write watch 4GiB reservation failed");
 auto* tail=static_cast<uint8_t*>(reservation.p)+0x100000000ull-page;
 if(!VirtualAllocFromApp(tail,page,MEM_COMMIT,PAGE_READWRITE))exception_report::raise("native write watch lazy commit failed");
 tail[page-1]=17;
 ULONG_PTR count=addresses.size();ULONG granularity=0;
 if(GetWriteWatch(WRITE_WATCH_FLAG_RESET,tail,page,addresses.data(),&count,&granularity) || count!=1 || addresses[0]!=tail)
  exception_report::raise("native write watch high guest address failed");
 LOG("[native write watch probe] PASS 4GiB reservation/lazy commit/high-address query; existing game memory untouched");
}
}
#endif
