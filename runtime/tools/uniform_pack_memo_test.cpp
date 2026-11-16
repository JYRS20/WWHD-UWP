// SPDX-License-Identifier: MPL-2.0
#include "../src/gfx/vulkan/uniform_pack_memo.h"
#include <array>
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char* reason) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); } }
int main() {
    gfxvk::UniformPackMemo memo;
    size_t budget=4096;
    check(memo.initialize({{8,16},{0,16},{2,16}},64,budget),"valid plan");
    std::array<uint32_t,32> regs{};
    for (size_t i=0;i<regs.size();++i) regs[i]=uint32_t(i);
    std::vector<uint8_t> packed(64,0x5a), copy;
    std::array<uint32_t,2> scale{0x3f800000,0x3f800000};uint64_t compared=0;
    check(!memo.matches(regs.data(),scale,compared),"unfilled memo must miss");
    memo.store(regs.data(),scale,packed);
    for (unsigned i=0;i<1000;++i) {
        check(memo.matches(regs.data(),scale,compared),"identical inputs must hit");
        memo.copy_to(copy);check(copy==packed,"all output bytes including padding preserved");
        copy[0]^=0xff; // bloom/AO modifies scratch after packing; cache must stay raw
    }
    for (unsigned word : {0u,1u,2u,3u,4u,5u,8u,9u,10u,11u}) {
        regs[word]^=0x80000000;check(!memo.matches(regs.data(),scale,compared),"every referenced source word must invalidate");regs[word]^=0x80000000;
    }
    regs[20]^=1;check(memo.matches(regs.data(),scale,compared),"unreferenced registers may change");
    scale[0]=0x80000000;check(!memo.matches(regs.data(),scale,compared),"float scale bit changes invalidate");
    scale[0]=0x3f800000;
    gfxvk::UniformPackMemo other;check(other.initialize({{12,4}},64,budget),"separate shader plan");
    check(!other.matches(regs.data(),scale,compared),"different shaders cannot inherit packed data");
    size_t exhausted=0;gfxvk::UniformPackMemo limited;
    check(!limited.initialize({{0,16}},64,exhausted),"budget exhaustion keeps fallback");
    gfxvk::UniformPackMemo invalid;check(!invalid.initialize({{0xffff,8}},64,budget),"out-of-range sources rejected");
    gfxvk::UniformPackMemo large;check(!large.initialize({},8193,budget),"oversize outputs bypass");
    std::puts("uniform_pack_memo_test: PASS");
}
