// SPDX-License-Identifier: MPL-2.0
#include "output_merger_commands.h"
#include <stdexcept>
namespace gfxd3d12 {
namespace {
D3D12_BLEND factor(uint32_t v,bool alpha) {
    if(alpha) {if(v==2)v=4;if(v==3)v=5;if(v==8)v=6;if(v==9)v=7;if(v==10)v=1;}
    constexpr D3D12_BLEND values[]{D3D12_BLEND_ZERO,D3D12_BLEND_ONE,D3D12_BLEND_SRC_COLOR,D3D12_BLEND_INV_SRC_COLOR,
        D3D12_BLEND_SRC_ALPHA,D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_DEST_ALPHA,D3D12_BLEND_INV_DEST_ALPHA,
        D3D12_BLEND_DEST_COLOR,D3D12_BLEND_INV_DEST_COLOR,D3D12_BLEND_SRC_ALPHA_SAT,D3D12_BLEND_SRC_ALPHA,
        D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_BLEND_FACTOR,D3D12_BLEND_INV_BLEND_FACTOR};
    if(v>=std::size(values)) throw std::invalid_argument("Unsupported D3D12 blend factor");return values[v];
}
D3D12_DEPTH_STENCILOP_DESC stencil_face(const StencilFaceState& f) {
    constexpr D3D12_STENCIL_OP ops[]{D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_ZERO,D3D12_STENCIL_OP_REPLACE,
        D3D12_STENCIL_OP_INCR_SAT,D3D12_STENCIL_OP_DECR_SAT,D3D12_STENCIL_OP_INVERT,D3D12_STENCIL_OP_INCR,D3D12_STENCIL_OP_DECR};
    if(f.compare>7 || f.fail>7 || f.depth_fail>7 || f.pass>7) throw std::invalid_argument("Invalid stencil face state");
    return {ops[f.fail],ops[f.depth_fail],ops[f.pass],static_cast<D3D12_COMPARISON_FUNC>(f.compare+1)};
}
D3D12_BLEND_OP operation(uint32_t v) {
    constexpr D3D12_BLEND_OP values[]{D3D12_BLEND_OP_ADD,D3D12_BLEND_OP_SUBTRACT,D3D12_BLEND_OP_MIN,D3D12_BLEND_OP_MAX,D3D12_BLEND_OP_REV_SUBTRACT};
    if(v>=std::size(values)) throw std::invalid_argument("Unsupported D3D12 blend operation");return values[v];
}
}
void apply_output_merger_state(D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,const OutputMergerState& s) {
    if(s.target_count>8 || s.depth_compare>7) throw std::invalid_argument("Invalid output-merger state");
    desc.BlendState={};desc.BlendState.IndependentBlendEnable=s.target_count>1;
    for(uint32_t i=0;i<8;++i) {
        const auto& b=s.targets[i];auto& out=desc.BlendState.RenderTarget[i];
        out.BlendEnable=i<s.target_count && b.enabled;
        out.SrcBlend=factor(b.source_color,false);out.DestBlend=factor(b.destination_color,false);out.BlendOp=operation(b.color_operation);
        out.SrcBlendAlpha=factor(b.source_alpha,true);out.DestBlendAlpha=factor(b.destination_alpha,true);out.BlendOpAlpha=operation(b.alpha_operation);
        out.LogicOp=D3D12_LOGIC_OP_NOOP;out.RenderTargetWriteMask=i<s.target_count?b.write_mask:0;
    }
    desc.DepthStencilState={};auto& d=desc.DepthStencilState;
    d.DepthEnable=s.depth_test;d.DepthWriteMask=s.depth_write?D3D12_DEPTH_WRITE_MASK_ALL:D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc=static_cast<D3D12_COMPARISON_FUNC>(s.depth_compare+1);
    d.StencilEnable=s.stencil_enabled;d.StencilReadMask=s.stencil_read_mask;d.StencilWriteMask=s.stencil_write_mask;
    d.FrontFace=stencil_face(s.stencil_front);d.BackFace=stencil_face(s.stencil_back);
}
void record_output_merger_constants(ID3D12GraphicsCommandList* cmd,const OutputMergerState& s) {
    if(!cmd) throw std::invalid_argument("Missing output-merger command list");cmd->OMSetBlendFactor(s.blend_constant.data());
    cmd->OMSetStencilRef(s.stencil_reference);
}
}
