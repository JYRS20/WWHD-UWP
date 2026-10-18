// SPDX-License-Identifier: MPL-2.0
#include "rasterizer_commands.h"
namespace gfxd3d12 {
bool apply_rasterizer_state(D3D12_GRAPHICS_PIPELINE_STATE_DESC& p,const RasterizerState& s) {
    p.RasterizerState={};auto& r=p.RasterizerState;r.FillMode=D3D12_FILL_MODE_SOLID;
    r.FrontCounterClockwise=s.front_counter_clockwise;r.DepthClipEnable=s.depth_clip;
    r.CullMode=s.cull_front&&!s.cull_back?D3D12_CULL_MODE_FRONT:
        s.cull_back&&!s.cull_front?D3D12_CULL_MODE_BACK:D3D12_CULL_MODE_NONE;
    return !s.skip&&!(s.cull_front&&s.cull_back);
}
}
