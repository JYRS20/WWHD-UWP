// SPDX-License-Identifier: MPL-2.0
#include "../src/gfx/vulkan/buffer_cache_core.h"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
// This backend must never trust an absent write tracker or its constant seq.
namespace wwatch {
uint64_t write_seq(){throw std::runtime_error("exact path called write_seq");}
uint64_t arm(uint32_t,uint32_t){throw std::runtime_error("exact path armed pages");}
bool written_since(uint32_t,uint32_t,uint64_t){throw std::runtime_error("exact path trusted write stamps");}
bool changed_since(uint32_t,uint32_t,uint64_t){throw std::runtime_error("exact path trusted changed stamps");}
}
using namespace gfxvk::bufcache;
static std::array<uint8_t,65536> guest;
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct TestBacking:Backing {
 struct Block {std::unique_ptr<uint8_t[]> data;uint64_t size;bool free=false;};
 std::vector<Block> blocks;std::vector<Region> pending;
 bool failNext=false;
 bool allocate(uint64_t n,Region& out)override {
  if(failNext){failNext=false;return false;}
  for(uint32_t i=0;i<blocks.size();++i)if(blocks[i].free&&blocks[i].size>=n){blocks[i].free=false;out={i,0,n,blocks[i].data.get()};return true;}
  blocks.push_back({std::make_unique<uint8_t[]>(n),n,false});out={uint32_t(blocks.size()-1),0,n,blocks.back().data.get()};return true;
 }
 void retire(const Region& r)override{pending.push_back(r);}
 void complete(){for(auto r:pending){require(!blocks[r.block].free,"double free");blocks[r.block].free=true;}pending.clear();}
};
static const uint8_t* source(uint32_t a,uint32_t n){return uint64_t(a)+n<=guest.size()?guest.data()+a:nullptr;}
static Status use(Cache& cache,uint32_t addr,uint32_t n,Entry*& e) {
 auto status=cache.lookup({addr,kRaw,0},n,e);
 if(status==kMiss)require(cache.upload(*e,guest.data()+addr,n),"upload failed");
 if(status!=kBypass){require(!memcmp(e->region.mapped,guest.data()+addr,n),"GPU bytes differ");require(!memcmp(e->region.mapped,e->shadow.data(),e->outSize),"CPU and GPU snapshots differ");}
 return status;
}
int main(){
 for(size_t n=0;n<guest.size();++n)guest[n]=uint8_t(n*37+11);
 TestBacking backing;Cache cache(backing);cache.exactRawSource=source;
 uint64_t frame=10;cache.set_frame(frame);Entry *a,*b;
 require(use(cache,4096,512,a)==kMiss,"initial lookup");Region original=a->region;
 for(int i=0;i<8;++i){cache.set_frame(frame+=10);require(use(cache,4096,512,a)==kHit,"did not persist across frames");}
 require(use(cache,4096,128,a)==kHit,"short prefix missed");
 guest[4096+511]^=7; // outside the small prefix, still inside stored identity
 cache.set_frame(frame+=10);require(use(cache,4096,128,a)==kHit,"unused tail invalidated a correct prefix");
 require(use(cache,4096,512,a)==kMiss,"larger request lost unannounced tail change");
 require(backing.blocks[original.block].data[511]!=guest[4096+511],"pending old region overwritten");
 require(!backing.pending.empty()&&!backing.blocks[original.block].free,"freed before fence");
 cache.set_frame(frame+=10);require(use(cache,4096,1024,a)==kMiss,"range did not grow");
 require(use(cache,4096+128,512,b)==kMiss,"overlap initial");
 guest[4096+300]^=3;cache.set_frame(frame+=10);
 require(use(cache,4096,1024,a)==kMiss,"first alias missed write");
 require(use(cache,4096+128,512,b)==kMiss,"second alias consumed write");
 require(use(cache,4096,1024,a)==kHit,"unchanged bytes missed");
 cache.invalidate_all();cache.set_frame(frame+=10);
 require(use(cache,4096,1024,a)==kMiss,"save-state epoch ignored");
 Entry* unsupported=reinterpret_cast<Entry*>(1);
 require(cache.lookup({8192,kIndexNative,0},16,unsupported)==kBypass&&!unsupported,"native indices accepted");
 require(cache.lookup({8192,kIndexConverted,0},16,unsupported)==kBypass&&!unsupported,"converted indices accepted");
 require(cache.lookup({0xfffffff0,kRaw,0},64,unsupported)==kBypass,"wrapped range accepted");
 require(cache.lookup({8192,kRaw,0},0,unsupported)==kBypass,"empty range accepted");
 uint32_t diff=0;std::vector<uint8_t> expected(a->shadow);expected[7]^=0xff;
 require(!cache.verify(*a,expected.data(),expected.size(),&diff)&&diff==7,"verification missed corruption");
 guest[4096+8]^=1;
 require(cache.verify(*a,guest.data()+4096,a->size)&&cache.stats.verifyRaced==1,"racing guest write misclassified");
 cache.set_frame(frame+=10);require(use(cache,4096,1024,a)==kMiss,"write remained stale");
 backing.failNext=true;Entry* failed;
 require(cache.lookup({30000,kRaw,0},256,failed)==kMiss&&!cache.upload(*failed,guest.data()+30000,256),"allocation failure did not bypass");
 require(cache.lookup({31000,kRaw,0},256,failed)==kMiss&&!cache.upload(*failed,guest.data()+31000,128),"mismatched raw extent accepted");
 auto oldResident=cache.resident_bytes();cache.set_frame(frame+=100);cache.end_frame(256);
 require(cache.resident_bytes()<oldResident,"budget eviction failed");
 require(!backing.pending.empty(),"eviction freed before fence");backing.complete();
 require(use(cache,4096,1024,a)!=kHit,"evicted range still hit");
 {TestBacking dynamicBacking;Cache dynamic(dynamicBacking);dynamic.exactRawSource=source;Entry* e;
  dynamic.set_frame(1);require(use(dynamic,20000,128,e)==kMiss,"dynamic initial");
  bool bypass=false;
  for(uint64_t f=2;f<7;++f){guest[20000]^=3;dynamic.set_frame(f);bypass|=use(dynamic,20000,128,e)==kBypass;}
  require(bypass&&dynamic.stats.becameDynamic,"dynamic backoff failed");
  dynamic.set_frame(100);require(use(dynamic,20000,128,e)==kMiss,"dynamic retry failed");
 }
 cache.clear();require(!cache.resident_bytes(),"clear left live regions");backing.complete();
 std::puts("persistent_raw_test: PASS (no tracker calls, frames/prefix/growth/tail/aliases, immutable retirement, epoch, indices bypass, verification/race, allocation failure, budget/churn)");
}
