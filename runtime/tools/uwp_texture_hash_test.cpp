// Validate the production AVX2 XXH3 against the baseline implementation, including guarded tails.
#include "gfx/vulkan/uwp_texture_hash.h"
#define XXH_INLINE_ALL
#include "../third_party/xxhash/xxhash.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <vector>

static void require(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
__attribute__((target("xsave"),noinline)) static unsigned long long xstate() { return _xgetbv(0); }
static bool has_avx2() {
 int bits[4];__cpuid(bits,0);if(bits[0]<7)return false;
 __cpuidex(bits,1,0);if((bits[2]&(1<<27))==0||(bits[2]&(1<<28))==0)return false;
 if((xstate()&6)!=6)return false;
 __cpuidex(bits,7,0);return (bits[1]&(1<<5))!=0;
}
__declspec(noinline) static uint64_t reference(const void* p,size_t n) { return XXH3_64bits(p,n); }
static uint64_t checks=0;
static void equal_hash(const void* p,size_t n) {
 require(reference(p,n)==gfxvk::uwp_texture_hash_avx2(p,n),"AVX2 differs from baseline XXH3");++checks;
}
static volatile uint64_t sink;
static double bench(uint64_t(*hash)(const void*,size_t),const std::vector<uint8_t>& b) {
 auto start=std::chrono::steady_clock::now();
 for(int repeat=0;repeat<20;repeat++) {const size_t offset=(repeat*17)%64;sink=hash(b.data()+offset,b.size()-offset);}
 return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
int main() {
 try {
  if(!has_avx2()) {std::puts("SKIP: CPU/OS does not support AVX2");return 77;}
  uint32_t random=0x9527351u;
  std::vector<uint8_t> bytes((34<<20)+64);
  for(auto& b:bytes) {random^=random<<13;random^=random>>17;random^=random<<5;b=uint8_t(random);}
  for(size_t offset=0;offset<64;offset++)for(size_t n=0;n<=512;n++)equal_hash(bytes.data()+offset,n);
  for(size_t n:{size_t(513),size_t(1023),size_t(1024),size_t(1025),size_t(4095),size_t(4096),size_t(4097),size_t(65535),size_t(65536),size_t(1<<20),size_t(34<<20)})
   for(size_t offset:{size_t(0),size_t(1),size_t(7),size_t(15),size_t(31),size_t(63)})equal_hash(bytes.data()+offset,n);
  auto before=reference(bytes.data(),4096);
  for(size_t at=0;at<4096;at++) {bytes[at]^=0x80;equal_hash(bytes.data(),4096);require(gfxvk::uwp_texture_hash_avx2(bytes.data(),4096)!=before,"byte mutation was missed");bytes[at]^=0x80;}
  SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
  auto* guarded=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*3,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  require(guarded!=nullptr,"guard allocation failed");
  DWORD old;require(VirtualProtect(guarded,page,PAGE_NOACCESS,&old)!=0,"leading guard failed");
  require(VirtualProtect(guarded+page*2,page,PAGE_NOACCESS,&old)!=0,"trailing guard failed");
  std::copy_n(bytes.data(),page,guarded+page);
  for(size_t n=0;n<=page;n++) {equal_hash(guarded+page,n);equal_hash(guarded+page*2-n,n);}
  require(VirtualFree(guarded,0,MEM_RELEASE)!=0,"guard free failed");
  const double baseline=bench(reference,bytes),avx2=bench(gfxvk::uwp_texture_hash_avx2,bytes);
  std::printf("PASS: %llu parity/mutation/guarded-tail checks; 20 x 34 MiB baseline %.2f ms, AVX2 %.2f ms (%.2fx; host microbenchmark only)\n",
   (unsigned long long)checks,baseline,avx2,baseline/avx2);
  return 0;
 } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
