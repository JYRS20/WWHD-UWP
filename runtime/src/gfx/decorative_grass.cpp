// SPDX-License-Identifier: MPL-2.0
// Experimental USA draw-only integration. Layout reference: public CC0 WWHD
// decomp 47e1dbc, d/d_grass.cpp (Grass_Draw / Grass_Update). No game assets.
#include "decorative_grass.h"
#include "decorative_grass_policy.h"
#include "runtime.h"
#include "recomp_table.h"
#include "guest_addr.h"
#include "gfx/stereo.h"
#include "gfx/renderer.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace decorative_grass {
namespace {
constexpr uint32_t drawAddress=0x0254C120,viewAddress=0x104B45F8;
constexpr uint32_t dataBase=0x9C,dataStride=0x44,dataCount=1500,packetSize=0x66408;
void(*callOriginal)(Cpu*,uint32_t)=nullptr;
enum class Mode {Off,Measure,Distance};
std::atomic<Mode> mode{Mode::Off};
std::atomic<bool> installed{false};
std::atomic<float> currentFov{0};
std::atomic<float> enterDistance{6000};
std::atomic<uint64_t> epoch{1};
std::atomic<bool> broken{false};
struct HistoryEntry {std::array<uint32_t,3> position{};bool known=false,visible=true;};
struct History {uint32_t packet=0;uint64_t generation=0;float distance=0;std::array<HistoryEntry,dataCount> entries{};};
thread_local History history;
thread_local DrawFilter* active=nullptr;
struct Counters {uint64_t packets=0,records=0,matched=0,wouldOmit=0,omitted=0,mismatches=0,rejected=0;std::array<uint64_t,5> bins{};};
thread_local Counters counters;
thread_local unsigned drawDepth=0;
bool range(uint32_t p,uint32_t n) {return p>=mem::kMem2Start&&uint64_t(p)+n<=mem::kMem2End;}
bool prepare(uint32_t packet,uint32_t state,DrawFilter& filter) {
 if(!range(packet,packetSize)||!range(state,0x34)||ld32(state+12)!=0||
    !view_permits_filter(currentFov.load(std::memory_order_relaxed),stereo::requestedMode!=stereo::Off))return false;
 std::array<float,12> view;
 for(size_t i=0;i<view.size();++i) {
  view[i]=float(ldf32(GD(viewAddress)+uint32_t(i)*4));
  if(!std::isfinite(view[i]))return false;
 }
 // Fail open for a non-view matrix instead of guessing a camera position.
 for(size_t row=0;row<3;++row) {
  float length=0;
  for(size_t column=0;column<3;++column)length+=view[row*4+column]*view[row*4+column];
  if(length<0.8f||length>1.2f)return false;
  for(size_t other=0;other<row;++other) {
   float dot=0;for(size_t column=0;column<3;++column)dot+=view[row*4+column]*view[other*4+column];
   if(std::fabs(dot)>0.1f)return false;
  }
 }
 const auto generation=epoch.load(std::memory_order_relaxed);
 // One atomic snapshot per packet: live UI changes cannot mix the enter and
 // leave thresholds or retain hysteresis decisions from another distance.
 const float enter=enterDistance.load(std::memory_order_relaxed),leave=enter*1.25f;
 if(history.packet!=packet||history.generation!=generation||history.distance!=enter) {
  history={};history.packet=packet;history.generation=generation;history.distance=enter;
 }
 std::array<bool,dataCount> seen{};
 // Preserve the exact room/list order used by the original draw. Never change
 // its visibility flags or packed packet offsets (also used by hit checking).
 for(uint32_t room=0;room<64;++room) {
  uint32_t data=ld32(packet+0x1A5CC+room*4);
  while(data) {
   const uint64_t first=uint64_t(packet)+dataBase;
   if(data<first||uint64_t(data)>=first+dataStride*dataCount||
      (data-uint32_t(first))%dataStride)return false;
   const auto index=(data-uint32_t(first))/dataStride;
   if(seen[index])return false;
   seen[index]=true;
   if(!(ld8(data+1)&2)) {
    const bool animated=int8_t(ld8(data+2))>=0;
    const auto indices=packet+0x661B0+(animated?0:24);
    const uint32_t count=ld32(indices+12);
    if(count) {
     const std::array<uint32_t,3> bits{ld32(data+4),ld32(data+8),ld32(data+12)};
     float position[3];for(size_t i=0;i<3;++i)std::memcpy(&position[i],&bits[i],4);
     float distance2=0;
     for(size_t row=0;row<3;++row) {
      float v=view[row*4+3];for(size_t i=0;i<3;++i)v+=view[row*4+i]*position[i];
      distance2+=v*v;
     }
     auto& old=history.entries[index];
     const bool previous=!old.known||old.position!=bits?true:old.visible;
     const bool visible=keep_visible(distance2,previous,enter,leave);
     old={bits,true,visible};
     const auto bin=distance2<1e6f?0:distance2<4e6f?1:distance2<16e6f?2:distance2<64e6f?3:4;
     ++counters.bins[bin];++counters.records;
     filter.rows.push_back({{ld32(indices+4),count,ld32(indices),ld32(indices+8),0,1},!visible});
    }
   }
   data=ld32(data+0x40);
  }
 }
 return true;
}
void draw(Cpu* c) {
 struct Depth {bool nested=drawDepth++!=0;~Depth(){--drawDepth;}} depth;
 // Explicit private-fixture regression of the same setter used by the menu.
 // No selector is included in distribution packages.
 static const bool testSwitch=[] {const char* e=std::getenv("WWHD_TEST_GRASS_TOGGLE");return e&&!std::strcmp(e,"1");}();
 static thread_local size_t nextSwitch=0;
 constexpr uint64_t switchFrames[4]={600,900,1200,1500};
 if(testSwitch&&nextSwitch<4&&render::frame_count()>=switchFrames[nextSwitch]) {
  set_enabled(nextSwitch%2==0);++nextSwitch;
  LOG("[decorative grass test] switch %zu at frame %llu",nextSwitch,(unsigned long long)render::frame_count());
 }
 // A nested or unsupported pass keeps all original behavior.
 if(depth.nested||drawing||mode.load(std::memory_order_relaxed)==Mode::Off||broken.load(std::memory_order_relaxed)||!range(c->r[4],0x34)||ld32(c->r[4]+12)!=0) {
  struct Pause {
   DrawFilter* previous=active;bool wasDrawing=drawing;
   Pause(){active=nullptr;drawing=false;}
   ~Pause(){active=previous;drawing=wasDrawing;}
  } pause;
  callOriginal(c,drawAddress);return;
 }
 static thread_local DrawFilter filter;
 filter.rows.clear();filter.consumed=0;filter.valid=true;filter.rows.reserve(dataCount);
 if(!prepare(c->r[3],c->r[4],filter)) {++counters.rejected;callOriginal(c,drawAddress);return;}
 struct Scope {
  DrawFilter* old;
  explicit Scope(DrawFilter& f):old(active){active=&f;drawing=true;}
  ~Scope(){active=old;drawing=old!=nullptr;}
 } scope(filter);
 callOriginal(c,drawAddress);
 ++counters.packets;
 if(!filter.complete()) {
  ++counters.mismatches;broken.store(true,std::memory_order_relaxed);
  LOG("[decorative grass] DISABLED: draw association mismatch; expected %zu consumed %zu",filter.rows.size(),filter.consumed);
 }
 if(counters.packets%120==0)LOG("[decorative grass] total packets %llu records %llu matched %llu would_omit %llu omitted %llu mismatches %llu rejected %llu; bins <1000/<2000/<4000/<8000/far %llu/%llu/%llu/%llu/%llu",
  (unsigned long long)counters.packets,(unsigned long long)counters.records,(unsigned long long)counters.matched,
  (unsigned long long)counters.wouldOmit,(unsigned long long)counters.omitted,(unsigned long long)counters.mismatches,
  (unsigned long long)counters.rejected,(unsigned long long)counters.bins[0],(unsigned long long)counters.bins[1],
  (unsigned long long)counters.bins[2],(unsigned long long)counters.bins[3],(unsigned long long)counters.bins[4]);
}
}
thread_local bool drawing=false;
bool omit_draw(uint32_t primitive,uint32_t count,uint32_t type,uint32_t indices,uint32_t base,uint32_t instances) {
 if(!active)return false;
 const bool omit=active->observe({primitive,count,type,indices,base,instances});
 if(!active->valid)return false;
 ++counters.matched;if(omit)++counters.wouldOmit;
 if(omit&&mode.load(std::memory_order_relaxed)==Mode::Distance) {++counters.omitted;return true;}
 return false;
}
void state_loaded(){epoch.fetch_add(1,std::memory_order_relaxed);currentFov.store(0,std::memory_order_relaxed);}
void view_fov(float fov){currentFov.store(fov,std::memory_order_relaxed);}
bool available(){return installed.load(std::memory_order_relaxed);}
bool healthy(){return !broken.load(std::memory_order_relaxed);}
bool enabled(){return healthy()&&mode.load(std::memory_order_relaxed)==Mode::Distance;}
void set_enabled(bool value) {
 if(!available()||!healthy())return;
 mode.store(value?Mode::Distance:Mode::Off,std::memory_order_relaxed);
 epoch.fetch_add(1,std::memory_order_relaxed);
 LOG("[decorative grass] session switch %s; distance %.0f",value?"on":"off",distance());
}
float distance(){return enterDistance.load(std::memory_order_relaxed);}
void set_distance(float value) {
 if(!valid_distance(value))return;
 enterDistance.store(value,std::memory_order_relaxed);
 LOG("[decorative grass] session distance %.0f game units",value);
}
void install(void(*original)(Cpu*,uint32_t),const std::function<bool(uint32_t,void(*)(Cpu*))>& replace) {
 const char* requested=std::getenv("WWHD_UWP_GRASS_MODE");
 if(!requested||!std::strcmp(requested,"off"))return;
 const bool ready=!std::strcmp(requested,"ready");
 mode=!std::strcmp(requested,"measure")?Mode::Measure:!std::strcmp(requested,"distance")?Mode::Distance:Mode::Off;
 if(mode.load()==Mode::Off&&!ready)return;
 // This first experiment is pinned to verified USA metadata only.
 if(std::strcmp(g_guest_build_title_id,"0005000010143500")) {LOG("[decorative grass] unavailable for this game build");mode=Mode::Off;return;}
 if(const char* value=std::getenv("WWHD_UWP_GRASS_DISTANCE")) {
  char* end=nullptr;const float distance=std::strtof(value,&end);
  if(end&&!*end&&valid_distance(distance))enterDistance.store(distance,std::memory_order_relaxed);
 }
 callOriginal=original;
 if(!replace(drawAddress,draw)) {mode=Mode::Off;LOG("[decorative grass] unavailable: original draw hook not free");return;}
 installed.store(true,std::memory_order_relaxed);
 LOG("[decorative grass] %s; enter %.0f leave %.0f game units; USA grass packet pass 0 only; narrow-camera/stereo fallback; no actor/packet/logic changes",
  ready?"ready (off)":mode.load()==Mode::Distance?"distance":"measure",distance(),distance()*1.25f);
}
}
