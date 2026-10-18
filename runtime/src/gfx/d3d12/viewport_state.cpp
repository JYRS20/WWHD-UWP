// SPDX-License-Identifier: MPL-2.0
#include "viewport_state.h"
#include "gx2/gx2_regs.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace gfxd3d12 {
ViewportState decode_viewport_state(std::span<const uint32_t> r,uint32_t w,uint32_t h,float sx,float sy) {
    using namespace Latte;
    if(r.size()<=REGADDR::PA_CL_VTE_CNTL || !w || !h || w>16384 || h>16384 ||
       !std::isfinite(sx) || !std::isfinite(sy) || sx<=0 || sy<=0 || sx>16 || sy>16)
        throw std::invalid_argument("Invalid GX2 viewport input");
    ViewportState s{};
    if(r[REGADDR::PA_CL_CLIP_CNTL]&(1u<<22)) {s.skip=true;return s;}
    auto f=[&](uint32_t reg){float v=std::bit_cast<float>(r[reg]);
        if(!std::isfinite(v)) throw std::invalid_argument("Non-finite GX2 viewport register");return v;};
    float xs=f(REGADDR::PA_CL_VPORT_XSCALE),xo=f(REGADDR::PA_CL_VPORT_XOFFSET),
          ys=f(REGADDR::PA_CL_VPORT_YSCALE),yo=f(REGADDR::PA_CL_VPORT_YOFFSET),
          zs=f(REGADDR::PA_CL_VPORT_ZSCALE),zo=f(REGADDR::PA_CL_VPORT_ZOFFSET);
    if(xs==0 || ys==0) {s.skip=true;return s;}
    if(xs<0 || ys>0) throw std::invalid_argument("GX2 viewport orientation needs a shader variant");
    s.x=(xo-xs)*sx;s.y=(yo+ys)*sy;s.width=2*xs*sx;s.height=-2*ys*sy;
    bool halfZ=(r[REGADDR::PA_CL_CLIP_CNTL]&(1u<<19))!=0;
    s.min_depth=halfZ?zo:zo-zs;s.max_depth=zo+zs;
    if(!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.width)||!std::isfinite(s.height)||
       s.x<-32768 || s.y<-32768 || s.x+s.width>32767 || s.y+s.height>32767 ||
       s.min_depth<0 || s.max_depth>1 || s.min_depth>s.max_depth)
        throw std::invalid_argument("Unsupported GX2 viewport bounds/depth range");
    s.window_scale_x=1/xs;s.window_scale_y=-1/ys;
    if(!std::isfinite(s.window_scale_x)||!std::isfinite(s.window_scale_y))
        throw std::invalid_argument("GX2 window transform overflows");
    const uint32_t tl=r[REGADDR::PA_SC_GENERIC_SCISSOR_TL],br=r[REGADDR::PA_SC_GENERIC_SCISSOR_BR];
    // Match upstream edge rounding, including tolerance at scaled screen edges.
    auto edge=[](uint32_t v,float scale,uint32_t limit,bool upper){
        double p=double(v)*scale;return int32_t(std::clamp(upper?std::ceil(p-0.01):std::floor(p+0.01),0.0,double(limit)));};
    s.scissor={edge(tl&0x7fff,sx,w,false),edge((tl>>16)&0x7fff,sy,h,false),
               edge(br&0x7fff,sx,w,true),edge((br>>16)&0x7fff,sy,h,true)};
    s.skip=s.scissor[2]<=s.scissor[0] || s.scissor[3]<=s.scissor[1];
    return s;
}
}
