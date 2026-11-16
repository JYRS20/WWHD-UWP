// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
struct Cpu;
namespace decorative_grass {
void install(void(*original)(Cpu*,uint32_t),const std::function<bool(uint32_t,void(*)(Cpu*))>& replace);
extern thread_local bool drawing;
bool omit_draw(uint32_t primitive,uint32_t count,uint32_t type,uint32_t indices,uint32_t base,uint32_t instances);
void state_loaded();
void view_fov(float fov);
bool available();
bool healthy();
bool enabled();
void set_enabled(bool enabled);
float distance();
void set_distance(float distance);
}
