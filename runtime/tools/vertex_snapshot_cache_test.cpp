// Exact-byte reuse, immutable GPU snapshots and generation scoping without a GPU dependency.
#include "gfx/vulkan/vertex_snapshot_cache.h"
#include "gfx/vulkan/vertex_snapshot_history.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace gfxvk;
struct Slice { uint64_t buffer=0;void* mapped=nullptr;size_t size=0; };
struct Arena {
 bool poison=true;
 std::vector<std::unique_ptr<uint8_t[]>> mapped;
 std::vector<std::vector<uint8_t>> gpu;
 Slice make(const void* data,size_t size) {
  mapped.emplace_back(new uint8_t[size]);
  std::memcpy(mapped.back().get(),data,size);gpu.emplace_back((const uint8_t*)data,(const uint8_t*)data+size);
  if(poison)std::memset(mapped.back().get(),0xcd,size);
  return {gpu.size(),mapped.back().get(),size};
 }
};
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
static void correctness(bool direct) {
 Arena a;a.poison=!direct;VertexSnapshotCache<Slice,4,2,128> cache;cache.begin(1,10);
 std::array<uint8_t,64> data{};
 auto make=[&](const void* p,size_t n){return a.make(p,n);};
 auto first=cache.get(0x1000,64,data.data(),direct,make);
 auto hit=cache.get(0x1000,64,data.data(),direct,make);
 require(first.buffer==hit.buffer,"same bytes did not reuse");
 // Every byte matters, including non-sampled words and the final byte.
 for(size_t at=0;at<data.size();at++) {
  auto previous=cache.get(0x1000,64,data.data(),direct,make);
  data[at]^=0x5b;auto next=cache.get(0x1000,64,data.data(),direct,make);
  require(previous.buffer!=next.buffer,"changed byte reused stale GPU data");
  require(a.gpu[next.buffer-1]==std::vector<uint8_t>(data.begin(),data.end()),"new GPU snapshot differs");
  require(a.gpu[first.buffer-1]==std::vector<uint8_t>(64,0),"old queued GPU snapshot was overwritten");
 }
 auto shortSlice=cache.get(0x1000,32,data.data(),direct,make);
 require(shortSlice.size==32,"size key was ignored");
 const auto allocations=a.gpu.size();cache.begin(1,11);
 cache.get(0x1000,32,data.data(),direct,make);require(a.gpu.size()==allocations+1,"retired generation was reused");
 cache.begin(2,11);cache.get(0x1000,32,data.data(),direct,make);
 require(a.gpu.size()==allocations+2,"different device reused GPU handles");
 auto stats=cache.stats();require(stats.shadowBytes<=(4*2*128),"CPU shadow budget exceeded");
 // One set forces LRU collisions and exercises metadata eviction without touching old GPU bytes.
 VertexSnapshotCache<Slice,1,2,128> collisions;collisions.begin(1,1);
 auto x=collisions.get(1,64,data.data(),direct,make);collisions.get(2,64,data.data(),direct,make);
 require(collisions.get(1,64,data.data(),direct,make).buffer==x.buffer,"LRU hit missing");
 collisions.get(3,64,data.data(),direct,make);
 const auto before=a.gpu.size();collisions.get(2,64,data.data(),direct,make);
 require(a.gpu.size()==before+1 && collisions.stats().evictions==2,"LRU eviction failed");
 std::array<uint8_t,129> big{};collisions.get(4,129,big.data(),direct,make);
 const auto beforeBig=a.gpu.size();collisions.get(4,129,big.data(),direct,make);
 require(a.gpu.size()==beforeBig+1,"oversized payload must bypass bounded cache");
}
int main() {
 try {
  correctness(false);correctness(true);
  Arena extended,baseline;VertexSnapshotCache<Slice> cache;VertexSnapshotHistory<Slice> history;
  cache.begin(1,1);std::array<uint8_t,4096> data{};
  for(unsigned repeat=0;repeat<100;repeat++)for(uint32_t key=0;key<8;key++) {
   data.fill(uint8_t(key));const uint32_t address=0x10000+key*4096;
   const auto cached=cache.get(address,data.size(),data.data(),false,[&](const void* p,size_t n){return extended.make(p,n);});
   auto* previous=VertexSnapshotHistory<Slice>::matches(history.last,address,data.size())?&history.last:history.secondary(address,data.size());
   Slice old;
   if(previous && VertexSnapshotHistory<Slice>::equal(*previous,data.data())) {if(previous!=&history.last)history.promote();old=history.last.slice;}
   else old=history.remember(address,data.size(),data.data(),true,false,[&](const void* p,size_t n){return baseline.make(p,n);});
   require(extended.gpu[cached.buffer-1]==baseline.gpu[old.buffer-1],"extended/baseline GPU bytes differ");
  }
  require(extended.gpu.size()==8 && baseline.gpu.size()==800,"alternating-range reuse regression");
  std::printf("PASS: poison-safe comparisons, all-byte mutations, size keys, LRU, device/generation reset, bounds; alternating 8 ranges: %zu vs %zu copies (synthetic workload only)\n",extended.gpu.size(),baseline.gpu.size());
  return 0;
 } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
