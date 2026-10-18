// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace gfxd3d12 {
struct TargetBlendState {
    bool enabled{};
    uint32_t source_color{1},destination_color{},color_operation{};
    uint32_t source_alpha{1},destination_alpha{},alpha_operation{};
    uint8_t write_mask{};
};
struct StencilFaceState {
    uint32_t compare{7},fail{},depth_fail{},pass{};
};
struct OutputMergerState {
    bool stencil_enabled{};
    uint8_t stencil_reference{},stencil_read_mask{0xff},stencil_write_mask{0xff};
    StencilFaceState stencil_front{},stencil_back{};
    bool depth_test{},depth_write{};
    uint32_t depth_compare{7},target_count{};
    std::array<TargetBlendState,8> targets{};
    std::array<float,4> blend_constant{};
};
// Depth/stencil compare/write, ordinary blend and colour write masks.
// Stencil supports independent face ops/functions with shared masks/reference.
// Logic ops, dual-source and constant-alpha factors require explicit extensions.
OutputMergerState decode_output_merger_state(std::span<const uint32_t> regs,
    uint32_t target_count,bool has_depth,uint8_t blendable_targets=0xff,bool has_stencil=false);
}
