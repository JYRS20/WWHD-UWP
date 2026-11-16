// SPDX-License-Identifier: MPL-2.0
#pragma once
#ifdef WWHD_BUILTIN_GC_MINIMAP
#include "mod_json.h"
#include <string>
namespace uwp_gc_minimap {
void configure();
mods::json::Value options();
mods::json::Value hd_options();
void draw_settings();
void begin_preparation(const std::string& source);
}
#endif
