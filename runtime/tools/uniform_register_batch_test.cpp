// SPDX-License-Identifier: MPL-2.0
#include "../src/gx2/uniform_register_batch.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>

// Differentially compare full register/shadow contents and classification.
// Include unchanged live data with deliberately stale context shadows.
int main() {
    constexpr uint32_t base=64, count=128, size=256;
    std::mt19937 rng(0x239ef);
    std::array<uint32_t,size> live{}, shadow{}, expected{}, expectedShadow{}, input{};
    for (unsigned trial=0;trial<100000;++trial) {
        for(unsigned i=0;i<size;++i)live[i]=rng(),shadow[i]=rng();
        expected=live;expectedShadow=shadow;
        const unsigned first=rng()%size,n=rng()%(size-first+1);
        for(unsigned i=0;i<n;++i)input[i]=(rng()%3)?live[first+i]:rng();
        const bool hasShadow=trial%2;
        const auto result=gx2::uniform_register_batch<base,count>(live.data(),first,input.data(),n);
        const bool inside=first>=base&&first<base+count&&n<=base+count-first;
        if ((result!=gx2::UniformRegisterBatch::outside)!=inside)std::abort();
        if(!inside)continue;
        bool dirty=false;
        for(unsigned i=0;i<n;++i) {
            dirty|=expected[first+i]!=input[i];
            expected[first+i]=input[i];
            if(hasShadow)expectedShadow[first+i]=input[i];
        }
        if((result==gx2::UniformRegisterBatch::changed)!=dirty)std::abort();
        if(result==gx2::UniformRegisterBatch::changed)
            std::memcpy(live.data()+first,input.data(),n*sizeof(uint32_t));
        if(hasShadow)std::memcpy(shadow.data()+first,input.data(),n*sizeof(uint32_t));
        if(live!=expected||shadow!=expectedShadow)std::abort();
    }
    for(unsigned first=0;first<size;++first)
        for(unsigned n=0;n<=size-first;++n) {
            const auto r=gx2::uniform_register_batch<base,count>(live.data(),first,live.data()+first,n);
            const bool inside=first>=base&&first<base+count&&n<=base+count-first;
            if(r!=(inside?gx2::UniformRegisterBatch::unchanged:gx2::UniformRegisterBatch::outside))std::abort();
        }
    std::puts("PASS 100000 differential batches, all boundaries, stale shadows and unchanged writes");
}
