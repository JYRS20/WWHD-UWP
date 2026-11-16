// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "driver_trace_stats.h"
namespace gfxvk {
inline bool command_trace_enabled() {
 static const bool enabled=[] {const char* e=std::getenv("WWHD_VK_COMMAND_TRACE");return e && !std::strcmp(e,"1");}();
 return enabled;
}
void install_command_trace();
void report_command_trace(uint64_t frame,double frames);
inline void note_command_replay(uint64_t elapsed,uint64_t commands,uint64_t payloadBytes,bool failed) {
 auto& replay=drivertrace::Store::state().replay;
 ++replay.batches;replay.commands+=commands;replay.payloadBytes+=payloadBytes;replay.ns+=elapsed;replay.failures+=failed;
}
}
