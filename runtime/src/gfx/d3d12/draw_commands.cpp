// SPDX-License-Identifier: MPL-2.0
#include "draw_commands.h"
#include <cstring>
#include <stdexcept>
namespace gfxd3d12 {
bool record_viewport_state(ID3D12GraphicsCommandList* cmd,const ViewportState& s) {
    if(!cmd) throw std::invalid_argument("Missing viewport command list");
    if(s.skip) return false;
    D3D12_VIEWPORT v{s.x,s.y,s.width,s.height,s.min_depth,s.max_depth};
    D3D12_RECT clip{s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]};
    cmd->RSSetViewports(1,&v);cmd->RSSetScissorRects(1,&clip);return true;
}
D3D12_PRIMITIVE_TOPOLOGY_TYPE pipeline_topology_type(PrimitiveTopology t) {
    switch(t) {
    case PrimitiveTopology::Points:return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::Lines:case PrimitiveTopology::LineStrip:return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::Triangles:case PrimitiveTopology::TriangleStrip:return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }throw std::invalid_argument("Invalid topology");
}
D3D12_INDEX_BUFFER_STRIP_CUT_VALUE pipeline_strip_cut(const DrawGeometry& g) {
    return g.restart?D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF:D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
}
winrt::com_ptr<ID3D12Resource> record_draw_geometry(ID3D12Device* device,ID3D12GraphicsCommandList* cmd,const DrawGeometry& g) {
    if(!device || !cmd) throw std::invalid_argument("Missing draw device/commands");
    if(!g.count || !g.instances) return {};
    D3D_PRIMITIVE_TOPOLOGY topology;
    switch(g.topology) {
    case PrimitiveTopology::Points:topology=D3D_PRIMITIVE_TOPOLOGY_POINTLIST;break;
    case PrimitiveTopology::Lines:topology=D3D_PRIMITIVE_TOPOLOGY_LINELIST;break;
    case PrimitiveTopology::LineStrip:topology=D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;break;
    case PrimitiveTopology::Triangles:topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;break;
    case PrimitiveTopology::TriangleStrip:topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;break;
    default:throw std::invalid_argument("Invalid draw topology");
    }
    if(g.restart && (!g.indexed || (g.topology!=PrimitiveTopology::LineStrip && g.topology!=PrimitiveTopology::TriangleStrip)))
        throw std::invalid_argument("Invalid draw restart state");
    if(!g.indexed) {
        if(g.base_vertex<0) throw std::invalid_argument("Negative non-indexed start vertex");
        cmd->IASetPrimitiveTopology(topology);cmd->DrawInstanced(g.count,g.instances,uint32_t(g.base_vertex),0);return {};
    }
    const uint64_t bytes=uint64_t(g.indices.size())*4;
    if(g.indices.size()!=g.count || bytes>UINT32_MAX) throw std::invalid_argument("Invalid converted index count");
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    winrt::com_ptr<ID3D12Resource> upload;
    winrt::check_hresult(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,__uuidof(ID3D12Resource),upload.put_void()));
    void* data=nullptr;D3D12_RANGE empty{0,0};winrt::check_hresult(upload->Map(0,&empty,&data));
    std::memcpy(data,g.indices.data(),size_t(bytes));D3D12_RANGE written{0,size_t(bytes)};upload->Unmap(0,&written);
    D3D12_INDEX_BUFFER_VIEW view{upload->GetGPUVirtualAddress(),UINT(bytes),DXGI_FORMAT_R32_UINT};
    cmd->IASetPrimitiveTopology(topology);cmd->IASetIndexBuffer(&view);
    cmd->DrawIndexedInstanced(g.count,g.instances,0,g.base_vertex,0);return upload;
}
}
