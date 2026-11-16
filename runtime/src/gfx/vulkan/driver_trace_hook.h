// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "driver_trace_stats.h"
#include <vulkan/vulkan_core.h>

namespace gfxvk::drivertrace {
// The original PFN signature/calling convention is retained. No argument copies
// beyond ordinary by-value function parameters; pointers/results pass through.
template<Kind K,class Function,class Storage=Store> struct Hook;
template<Kind K,class Storage,class Result,class... Args>
struct Hook<K,Result(VKAPI_PTR*)(Args...),Storage> {
 using Function=Result(VKAPI_PTR*)(Args...);
 inline static Function original=nullptr;
 static VKAPI_ATTR Result VKAPI_CALL invoke(Args... args) {
  auto& state=Storage::state();
  const auto token=state.begin(K);
  struct End {decltype(state)& trace;decltype(token) sample;~End(){trace.end(sample);}} end{state,token};
  return original(args...);
 }
 static void install(Function& function) {
  if(function && function!=&invoke) {original=function;function=&invoke;}
 }
};
} // namespace gfxvk::drivertrace
