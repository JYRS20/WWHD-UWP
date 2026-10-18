// SPDX-License-Identifier: MPL-2.0
#include "rasterizer_state.h"
#include "gx2/gx2_regs.h"
#include <cstring>
#include <stdexcept>
namespace gfxd3d12 {
RasterizerState decode_rasterizer_state(std::span<const uint32_t> r) {
    using namespace Latte;
    if(r.size()<=REGADDR::PA_SU_SC_MODE_CNTL) throw std::invalid_argument("Truncated GX2 rasterizer registers");
    LATTE_PA_SU_SC_MODE_CNTL mode{};std::memcpy(&mode,&r[REGADDR::PA_SU_SC_MODE_CNTL],4);
    LATTE_PA_CL_CLIP_CNTL clip{};std::memcpy(&clip,&r[REGADDR::PA_CL_CLIP_CNTL],4);
    RasterizerState s{};s.cull_front=mode.get_CULL_FRONT();s.cull_back=mode.get_CULL_BACK();
    s.front_counter_clockwise=mode.get_FRONT_FACE()==LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW;
    s.skip=(s.cull_front&&s.cull_back)||clip.get_DX_RASTERIZATION_KILL();
    if(s.skip) return s;
    if(static_cast<uint32_t>(mode.get_POLYGON_MODE())!=0 || mode.get_OFFSET_FRONT_ENABLED() ||
       mode.get_OFFSET_BACK_ENABLED() || mode.get_OFFSET_PARA_ENABLED())
        throw std::invalid_argument("GX2 polygon mode/depth bias needs D3D12 adaptation");
    if(clip.get_CLIP_DISABLE() || clip.get_ZCLIP_NEAR_DISABLE()!=clip.get_ZCLIP_FAR_DISABLE())
        throw std::invalid_argument("GX2 asymmetric/general clip disable is unsupported");
    s.depth_clip=!clip.get_ZCLIP_NEAR_DISABLE();return s;
}
}
