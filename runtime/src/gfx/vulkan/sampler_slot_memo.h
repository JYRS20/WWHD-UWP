// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace gfxvk {
struct SamplerSlotKey {
 std::array<uint32_t,3> words{};
 uint32_t flags=0;
 static SamplerSlotKey make(const uint32_t* words,bool compare,bool integer,bool aniso,bool nearest) {
  SamplerSlotKey key;std::memcpy(key.words.data(),words,12);
  key.flags=uint32_t(compare)|(uint32_t(integer)<<1)|(uint32_t(aniso)<<2)|(uint32_t(nearest)<<3);
  return key;
 }
 bool operator==(const SamplerSlotKey&) const = default;
};
// Non-owning handles. The original sampler map owns the objects and retains
// their existing lifetime. Compare every key word/flag; slot or hash alone
// never establishes identity. No guest texture contents are memoized.
template<class Device,class Value,size_t Slots> class SamplerSlotMemo {
 struct Entry {SamplerSlotKey key{};Value value{};bool valid=false;};
 struct Slot {std::array<Entry,2> entries{};};
 std::array<Slot,Slots> slots_{};
 Device device_{};bool initialized_=false;
 void device(Device next) {
  if(!initialized_ || device_!=next){slots_={};device_=next;initialized_=true;}
 }
public:
 Value find(Device next,size_t slot,const SamplerSlotKey& key) {
  device(next);if(slot>=Slots)return {};
  auto& entries=slots_[slot].entries;
  if(entries[0].valid && entries[0].key==key)return entries[0].value;
  if(entries[1].valid && entries[1].key==key){std::swap(entries[0],entries[1]);return entries[0].value;}
  return {};
 }
 void remember(Device next,size_t slot,const SamplerSlotKey& key,Value value) {
  device(next);if(slot>=Slots || !value)return;
  auto& entries=slots_[slot].entries;
  if(entries[0].valid && entries[0].key==key){entries[0].value=value;return;}
  entries[1]=entries[0];entries[0]={key,value,true};
 }
 void reset(){slots_={};device_={};initialized_=false;}
};
} // namespace gfxvk
