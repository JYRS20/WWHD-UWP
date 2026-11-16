// SPDX-License-Identifier: MPL-2.0
#include "loader.h"
#include "command_trace.h"
#include "driver_trace_hook.h"
#include <cstdlib>
#include <cstring>
extern void log_msg(const char*,...);

namespace gfxvk {
void install_command_trace() {
 if(!command_trace_enabled())return;
#define WWHD_INSTALL_TRACE(name) drivertrace::Hook<drivertrace::Kind::name,PFN_##name>::install(name);
 WWHD_DRIVER_TRACE_APIS(WWHD_INSTALL_TRACE)
#undef WWHD_INSTALL_TRACE
 log_msg("[vulkan driver trace] enabled: varied-position 1/64 per API; caller-wall estimates, not GPU busy time; thread-local counters; report covers the calling render thread only");
}
void report_command_trace(uint64_t frame,double frames) {
 if(!command_trace_enabled() || frames<=0)return;
 auto& trace=drivertrace::Store::state();
 double commandNs=0,descriptorNs=0;uint64_t calls=0,samples=0,uncovered=0;
 for(size_t i=0;i<drivertrace::kKinds;++i) {
  const auto& c=trace.cells[i].counts;if(!c.calls)continue;
  calls+=c.calls;samples+=c.samples;
  if(!c.samples) {
   uncovered+=c.calls;
   log_msg("[vulkan driver API] frame %llu %s: %.2f calls/frame; 0/%llu timed; estimate unavailable",
    (unsigned long long)frame,drivertrace::names[i],c.calls/frames,(unsigned long long)c.calls);
   continue;
  }
  const double estimate=c.estimated_ns();
  const bool descriptor=i==size_t(drivertrace::Kind::vkAllocateDescriptorSets) || i==size_t(drivertrace::Kind::vkUpdateDescriptorSets);
  (descriptor?descriptorNs:commandNs)+=estimate;
  log_msg("[vulkan driver API] frame %llu %s: %.2f calls/frame; %llu/%llu timed; est %.4f ms/frame; sampled max %.4f ms/call",
   (unsigned long long)frame,drivertrace::names[i],c.calls/frames,(unsigned long long)c.samples,(unsigned long long)c.calls,estimate/1e6/frames,c.maxNs/1e6);
 }
 log_msg("[vulkan driver API total] frame %llu: recording est %.3f ms/frame; descriptor preparation est %.3f ms/frame; %llu/%llu timed; nested excluded %llu; %llu calls in APIs without samples omitted from estimate; selected APIs only, caller-wall time may include scheduling; subset of draw phases, DO NOT add to render CPU or submission timers",
  (unsigned long long)frame,commandNs/1e6/frames,descriptorNs/1e6/frames,(unsigned long long)samples,(unsigned long long)calls,(unsigned long long)trace.nested,(unsigned long long)uncovered);
 const auto& replay=trace.replay;
 log_msg("[vulkan command replay] frame %llu: %.2f batches/frame; %.2f completed callbacks/frame; %.3f MiB declared payload/frame; %.3f ms/frame elapsed; %llu failures; exact whole-tail scopes INCLUDE callbacks, API calls, waits and submissions, DO NOT sum with API timings",
  (unsigned long long)frame,replay.batches/frames,replay.commands/frames,replay.payloadBytes/double(1<<20)/frames,replay.ns/1e6/frames,(unsigned long long)replay.failures);
 trace.reset_window();
}
}
