// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace gfxd3d12 {
struct ViewportState {
    float x{},y{},width{},height{},min_depth{},max_depth{};
    float window_scale_x{},window_scale_y{};
    std::array<int32_t,4> scissor{};
    bool skip{};
};
// Initial subset: positive X / negative Y guest scales, ordinary [0,1] depth.
// Unsupported orientation/depth throws; no shader reflection or camera matrix is implied.
ViewportState decode_viewport_state(std::span<const uint32_t> regs,uint32_t target_width,
    uint32_t target_height,float resolution_x=1,float resolution_y=1);
}
