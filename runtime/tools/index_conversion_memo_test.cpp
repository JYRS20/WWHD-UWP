// SPDX-License-Identifier: MPL-2.0
#include "../src/gfx/vulkan/index_conversion_memo.h"
#include "../src/gfx/vulkan/index_conversion.h"
#include <cstdio>
#include <stdexcept>
static void require(bool ok){if(!ok)throw std::runtime_error("index memo assertion failed");}
int main() {
 gfxvk::IndexConversionMemo<4,2,4096,16384> memo;
 std::vector<uint8_t> data(4096);uint32_t rng=19331;
 for(auto& v:data){rng=rng*1664525+1013904223;v=uint8_t(rng>>24);}
 size_t cases=0;
 for(uint32_t type:{0u,1u,4u,9u})for(uint32_t primitive:{1u,2u,3u,4u,5u,6u,0x12u,0x13u,0x14u})
 for(uint32_t count:{0u,1u,2u,3u,4u,9u,32u,127u})for(bool restart:{false,true}) {
  const uint32_t width=(type==0||type==4)?2:4,readCount=primitive==0x12?std::max(count,1u):count;
  gfxvk::IndexMemoKey key{0x10000000,readCount*width,primitive,count,type,0x10203,restart};
  auto convert=[&](const void* p,std::vector<uint32_t>& out){gfxvk::vk::convert_indices(p,primitive,count,type,restart,key.marker,out);};
  std::vector<uint32_t> original;convert(data.data()+1,original); // unaligned
  auto* first=memo.get(key,data.data()+1,original.size(),false,convert);
  if(!key.bytes){require(!first);continue;}
  require(first&&*first==original);
  auto before=memo.stats().hits;
  auto* second=memo.get(key,data.data()+1,original.size(),true,convert);
  require(second&&*second==original&&memo.stats().hits==before+1);
  data[key.bytes]^=17;convert(data.data()+1,original); // change final byte
  auto* changed=memo.get(key,data.data()+1,original.size(),true,convert);
  require(changed&&*changed==original&&memo.stats().hits==before+1);
  ++cases;
 }
 // Same bytes at different addresses and changed semantic parameters are misses.
 gfxvk::IndexMemoKey key{0x20000000,16,4,4,9,0,false};
 auto convert=[&](const void* p,std::vector<uint32_t>& out){gfxvk::vk::convert_indices(p,key.primitive,key.count,key.type,key.restart,key.marker,out);};
 require(memo.get(key,data.data(),4,false,convert));auto hits=memo.stats().hits;
 key.address+=4096;require(memo.get(key,data.data(),4,false,convert));require(memo.stats().hits==hits);
 key.restart=true;key.marker=0x12345678;require(memo.get(key,data.data(),4,true,convert));require(memo.stats().hits==hits);
 key.bytes=4097;require(!memo.get(key,data.data(),4,false,convert));
 key.bytes=16;require(!memo.get(key,data.data(),4097,false,convert));
 std::printf("index_conversion_memo_test: PASS (%zu normalization cases, exact-byte tail mutation, flags/address, bounds, replacement/verification)\n",cases);
}
