// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <span>
namespace gfxd3d12 {
struct RasterizerState {
    bool cull_front{},cull_back{},front_counter_clockwise{true},depth_clip{true},skip{};
};
// Matches the viewport subset: positive guest X scale and negative Y scale, no shader Y flip.
// Fill triangles only; depth bias, special polygon modes and asymmetric Z clip are rejected.
RasterizerState decode_rasterizer_state(std::span<const uint32_t> regs);
}
