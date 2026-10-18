// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace gfxd3d12 {
enum class PrimitiveTopology { Points, Lines, LineStrip, Triangles, TriangleStrip };
struct DrawGeometry {
    PrimitiveTopology topology{};
    uint32_t count{}, instances{};
    int32_t base_vertex{};
    bool indexed{}, restart{};
    std::vector<uint32_t> indices;
};
// Input span starts at the already-validated guest index address. No guest pointers retained.
// Types: 0/1 = little-endian 16/32 bits, 4/9 = big-endian 16/32 bits.
// Indexed flag must match the GX2 command; an empty indexed span is rejected.
// Restart is supported only for indexed strips.
DrawGeometry prepare_draw_geometry(uint32_t primitive,uint32_t count,uint32_t index_type,
    std::span<const uint8_t> index_data,uint32_t base_vertex,uint32_t instances,
    bool indexed,bool restart=false,uint32_t restart_marker=0xffffffffu);
}
