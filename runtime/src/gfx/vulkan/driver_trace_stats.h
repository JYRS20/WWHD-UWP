// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "../../profile_sampling.h"
#include <array>
#include <chrono>
#include <cstdint>

// Selected device entry points, never query/presentation/queue/fence APIs.
#define WWHD_DRIVER_TRACE_APIS(X) \
 X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdDispatch) \
 X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) \
 X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetBlendConstants) \
 X(vkCmdSetStencilReference) X(vkCmdSetStencilWriteMask) X(vkCmdPushConstants) \
 X(vkCmdPipelineBarrier) X(vkCmdBeginRendering) X(vkCmdEndRendering) \
 X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
 X(vkCmdCopyBuffer) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdCopyImage) \
 X(vkCmdBlitImage) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) \
 X(vkBeginCommandBuffer) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets)

namespace gfxvk::drivertrace {
enum class Kind : uint32_t {
#define WWHD_TRACE_KIND(name) name,
 WWHD_DRIVER_TRACE_APIS(WWHD_TRACE_KIND)
#undef WWHD_TRACE_KIND
 Count
};
constexpr size_t kKinds=size_t(Kind::Count);
inline constexpr const char* names[]={
#define WWHD_TRACE_NAME(name) #name,
 WWHD_DRIVER_TRACE_APIS(WWHD_TRACE_NAME)
#undef WWHD_TRACE_NAME
};
struct Clock {
 static uint64_t now() {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
};
struct Counts {
 uint64_t calls=0,samples=0,ns=0,maxNs=0;
 double estimated_ns() const {return samples?double(ns)*double(calls)/double(samples):0;}
};
struct ReplayCounts {uint64_t batches=0,commands=0,payloadBytes=0,ns=0,failures=0;};
// All state is caller/thread-owned. Outer API elapsed time includes any nested
// call but the nested call is not counted/timed again. GPU/helper-thread work
// outside this caller is not measured. Replays are a separate overlapping scope.
template<class Time=Clock,class Sampler=rprof::OperationSampler> class State {
public:
 struct Cell {Counts counts;Sampler sampler{1};};
 struct Token {Kind kind;uint64_t start=0;bool sampled=false,outer=false;};
 std::array<Cell,kKinds> cells{};
 ReplayCounts replay;
 uint64_t nested=0;
 State() {
  for(size_t i=0;i<kKinds;++i)cells[i].sampler=Sampler(uint32_t(0x9e3779b9u+i*0x85ebca6bu));
 }
 Token begin(Kind kind) {
  Token token{kind};token.outer=depth_++==0;
  if(!token.outer) {++nested;return token;}
  auto& cell=cells[size_t(kind)];++cell.counts.calls;
  token.sampled=cell.sampler.take();if(token.sampled)token.start=Time::now();
  return token;
 }
 void end(Token token) {
  if(token.sampled) {
   const uint64_t elapsed=Time::now()-token.start;
   auto& count=cells[size_t(token.kind)].counts;
   ++count.samples;count.ns+=elapsed;if(elapsed>count.maxNs)count.maxNs=elapsed;
  }
  --depth_;
 }
 void reset_window() {
  for(auto& cell:cells)cell.counts={};
  replay={};nested=0; // keep sampling positions and caller nesting intact
 }
 uint32_t depth() const {return depth_;}
private:
 uint32_t depth_=0;
};
struct Store {
 static State<>& state() {static thread_local State<> value;return value;}
};
} // namespace gfxvk::drivertrace
