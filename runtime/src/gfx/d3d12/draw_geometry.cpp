// SPDX-License-Identifier: MPL-2.0
#include "draw_geometry.h"
#include "gfx/vulkan/index_conversion.h"
#include <bit>
namespace gfxd3d12 {
DrawGeometry prepare_draw_geometry(uint32_t primitive,uint32_t count,uint32_t index_type,
    std::span<const uint8_t> index_data,uint32_t base_vertex,uint32_t instances,
    bool indexed,bool restart,uint32_t restart_marker) {
    DrawGeometry result{};
    switch(primitive) {
    case 1: result.topology=PrimitiveTopology::Points;break;
    case 2: result.topology=PrimitiveTopology::Lines;break;
    case 3: case 0x12: result.topology=PrimitiveTopology::LineStrip;break;
    case 4: case 5: case 0x13: case 0x14: result.topology=PrimitiveTopology::Triangles;break;
    case 6: result.topology=PrimitiveTopology::TriangleStrip;break;
    default: throw std::invalid_argument("Unsupported GX2 primitive");
    }
    result.base_vertex=std::bit_cast<int32_t>(base_vertex);result.instances=instances;
    if(!count || !instances) return result;
    // Bound CPU conversion and the eventual UINT-sized D3D12 index buffer.
    if(count>16u*1024*1024) throw std::invalid_argument("GX2 draw exceeds index conversion budget");
    if(!indexed && !index_data.empty()) throw std::invalid_argument("Index bytes supplied for a non-indexed draw");
    if(restart && (!indexed || (primitive!=3 && primitive!=6)))
        throw std::invalid_argument("GX2 restart requires an indexed line/triangle strip");
    if(indexed) {
        uint32_t bytes;
        switch(index_type) {case 0:case 4:bytes=2;break;case 1:case 9:bytes=4;break;
        default:throw std::invalid_argument("Unsupported GX2 index type");}
        if(uint64_t(count)*bytes>index_data.size()) throw std::out_of_range("Truncated GX2 index buffer");
    }
    // Reuse upstream conversion; this header has no Vulkan API dependency.
    gfxvk::vk::convert_indices(indexed?index_data.data():nullptr,primitive,count,index_type,
        restart,restart_marker,result.indices);
    result.indexed=indexed || !result.indices.empty();
    result.count=result.indexed?static_cast<uint32_t>(result.indices.size()):count;
    if(!indexed && ((primitive==5 && count<3) || ((primitive==0x13||primitive==0x14) && count<4)))
        result.count=0;
    result.restart=restart;
    return result;
}
}
