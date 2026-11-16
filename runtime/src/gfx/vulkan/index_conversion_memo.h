// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include "../../exception_report.h"

namespace gfxvk {
struct IndexMemoKey {
 uint32_t address=0,bytes=0,primitive=0,count=0,type=0,marker=0;
 bool restart=false;
 bool operator==(const IndexMemoKey&) const=default;
};
struct IndexMemoStats {uint64_t requests=0,hits=0,verified=0,inputCopied=0,outputReused=0,evictions=0;};
// CPU-only, bounded, exact byte comparison. Cached vectors own their bytes;
// GPU uploads still use the original per-submission immutable snapshot path.
template<size_t Sets=32,size_t Ways=4,size_t MaxInput=65536,size_t MaxOutput=262144>
class IndexConversionMemo {
 struct Entry {IndexMemoKey key{};std::vector<uint8_t> input;std::vector<uint32_t> output;uint64_t age=0;bool valid=false;};
 std::array<std::array<Entry,Ways>,Sets> entries{};
 uint64_t clock=0;
 IndexMemoStats counters{};
 std::vector<uint32_t> reference;
public:
 IndexMemoStats stats()const{return counters;}
 template<class Convert>
 const std::vector<uint32_t>* get(const IndexMemoKey& key,const void* fresh,size_t expectedOutputWords,bool verify,Convert&& convert) {
  if(!fresh || !key.bytes || key.bytes>MaxInput || expectedOutputWords>MaxOutput/4)return nullptr;
  ++counters.requests;
  uint64_t hash=uint64_t(key.address)*0x9e3779b97f4a7c15ull+key.bytes;
  hash^=hash>>32;
  auto& set=entries[hash%Sets];Entry* chosen=nullptr;
  for(auto& e:set) {
   if(!e.valid || e.key!=key)continue;
   if(!std::memcmp(fresh,e.input.data(),key.bytes)) {
    ++counters.hits;counters.outputReused+=e.output.size()*4;e.age=++clock;
    if(verify) {
     // Use the identical retained input, never a racing second guest read.
     convert(e.input.data(),reference);
     if(reference!=e.output)exception_report::raise("index conversion memo verification mismatch");
     ++counters.verified;
    }
    return &e.output;
   }
   chosen=&e;break; // a changed version of this key replaces the old one
  }
  if(!chosen) {
   for(auto& e:set)if(!e.valid){chosen=&e;break;}
   if(!chosen){chosen=&set[0];for(auto& e:set)if(e.age<chosen->age)chosen=&e;++counters.evictions;}
  }
  chosen->valid=false;
  // Exact capacities bound retained input+output storage to <=40 MiB at defaults.
  if(chosen->input.capacity()<key.bytes)std::vector<uint8_t>(key.bytes).swap(chosen->input);
  else chosen->input.resize(key.bytes);
  std::memcpy(chosen->input.data(),fresh,key.bytes);
  if(chosen->output.capacity()<expectedOutputWords)std::vector<uint32_t>(expectedOutputWords).swap(chosen->output);
  convert(chosen->input.data(),chosen->output);
  if(chosen->output.size()!=expectedOutputWords)exception_report::raise("index conversion memo output extent mismatch");
  counters.inputCopied+=key.bytes;
  chosen->key=key;chosen->age=++clock;chosen->valid=true;
  return &chosen->output;
 }
};
}
