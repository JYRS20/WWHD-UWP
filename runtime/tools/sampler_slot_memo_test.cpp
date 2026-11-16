// SPDX-License-Identifier: MPL-2.0
#include "../src/gfx/vulkan/sampler_slot_memo.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 gfxvk::SamplerSlotMemo<uintptr_t,uintptr_t,36> cache;
 uint32_t words[3]={0x12345678,0x87654321,0xaabbccdd};
 auto key=gfxvk::SamplerSlotKey::make(words,false,false,false,false);
 check(cache.find(1,0,key)==0,"empty lookup");cache.remember(1,0,key,100);
 check(cache.find(1,0,key)==100 && cache.find(1,18,key)==0,"stage/unit isolation");
 for(unsigned i=0;i<3;++i){words[i]^=1;auto changed=gfxvk::SamplerSlotKey::make(words,false,false,false,false);check(cache.find(1,0,changed)==0,"every raw sampler word must match");words[i]^=1;}
 for(unsigned flags=1;flags<16;++flags){auto changed=gfxvk::SamplerSlotKey::make(words,flags&1,flags&2,flags&4,flags&8);check(cache.find(1,0,changed)==0,"every sampler flag must match");}
 auto b=key,c=key;b.words[0]^=1;c.words[1]^=1;
 cache.remember(1,0,b,200);check(cache.find(1,0,key)==100,"second entry found and promoted");
 cache.remember(1,0,c,300);check(cache.find(1,0,b)==0 && cache.find(1,0,key)==100 && cache.find(1,0,c)==300,"bounded two-entry replacement");
 cache.remember(1,18,key,400);check(cache.find(1,18,key)==400,"same key can be used by another stage");
 check(cache.find(2,0,key)==0 && cache.find(2,18,key)==0,"device replacement invalidates all entries");
 cache.remember(2,0,key,500);cache.reset();check(cache.find(2,0,key)==0,"explicit reset covers repeated device identity");
 cache.remember(2,36,key,700);check(cache.find(2,36,key)==0,"invalid slot uses fallback");
 cache.remember(2,0,key,0);check(cache.find(2,0,key)==0,"null object is never published");
 check(sizeof(cache)<4096,"fixed bounded storage");
 std::puts("sampler_slot_memo_test: PASS (words, flags, units/stages, replacement, device/reset, bounds)");
}
