#include "gfx/d3d12/viewport_state.h"
#include "gx2/gx2_regs.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace gfxd3d12;
using namespace Latte;
static void require(bool v) {if(!v) throw std::runtime_error("GX2 viewport test mismatch");}
static auto registers() {
    std::vector<uint32_t> r(gx2::kNumRegs);
    auto set=[&](uint32_t reg,float value){r[reg]=std::bit_cast<uint32_t>(value);};
    set(REGADDR::PA_CL_VPORT_XSCALE,640);set(REGADDR::PA_CL_VPORT_XOFFSET,640);
    set(REGADDR::PA_CL_VPORT_YSCALE,-360);set(REGADDR::PA_CL_VPORT_YOFFSET,360);
    set(REGADDR::PA_CL_VPORT_ZSCALE,0.5f);set(REGADDR::PA_CL_VPORT_ZOFFSET,0.5f);
    r[REGADDR::PA_SC_GENERIC_SCISSOR_BR]=1280|(720<<16);return r;
}
int main(){try {
    auto r=registers();auto s=decode_viewport_state(r,1280,720);
    require(s.x==0&&s.y==0&&s.width==1280&&s.height==720&&s.min_depth==0&&s.max_depth==1&&!s.skip);
    require(s.scissor==std::array<int32_t,4>{0,0,1280,720});
    require(std::abs(s.window_scale_x-2.0f/1280)<1e-9f&&std::abs(s.window_scale_y-2.0f/720)<1e-9f);
    r[REGADDR::PA_CL_VPORT_XOFFSET]=std::bit_cast<uint32_t>(650.0f);
    r[REGADDR::PA_CL_VPORT_YOFFSET]=std::bit_cast<uint32_t>(380.0f);
    r[REGADDR::PA_SC_GENERIC_SCISSOR_TL]=100|(50<<16);r[REGADDR::PA_SC_GENERIC_SCISSOR_BR]=200|(150<<16);
    s=decode_viewport_state(r,1920,1080,1.5f,1.5f);
    require(s.x==15&&s.y==30&&s.width==1920&&s.height==1080&&s.scissor==std::array<int32_t,4>{150,75,300,225});
    r=registers();r[REGADDR::PA_SC_GENERIC_SCISSOR_BR]=720|(480<<16);
    s=decode_viewport_state(r,1200,800,1200.f/720,800.f/480);
    require(s.scissor[2]==1200&&s.scissor[3]==800);
    r=registers();r[REGADDR::PA_CL_CLIP_CNTL]=1u<<19;
    r[REGADDR::PA_CL_VPORT_ZOFFSET]=std::bit_cast<uint32_t>(0.25f);
    s=decode_viewport_state(r,1280,720);require(s.min_depth==0.25f&&s.max_depth==0.75f);
    for(int mode=0;mode<3;++mode){r=registers();
        if(mode==0)r[REGADDR::PA_CL_CLIP_CNTL]=1u<<22;
        if(mode==1)r[REGADDR::PA_CL_VPORT_YSCALE]=0;
        if(mode==2)r[REGADDR::PA_SC_GENERIC_SCISSOR_TL]=2000|(1000<<16);
        require(decode_viewport_state(r,1280,720).skip);
    }
    for(int mode=0;mode<8;++mode){r=registers();bool rejected=false;try {
        if(mode==0)r.resize(4);
        if(mode==1)r[REGADDR::PA_CL_VPORT_YSCALE]=std::bit_cast<uint32_t>(360.f);
        if(mode==2)r[REGADDR::PA_CL_VPORT_XSCALE]=std::bit_cast<uint32_t>(-640.f);
        if(mode==3)r[REGADDR::PA_CL_VPORT_XOFFSET]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
        if(mode==4)r[REGADDR::PA_CL_VPORT_ZOFFSET]=std::bit_cast<uint32_t>(2.f);
        if(mode==5)r[REGADDR::PA_CL_VPORT_ZSCALE]=std::bit_cast<uint32_t>(-0.5f);
        if(mode==6)r[REGADDR::PA_CL_VPORT_XOFFSET]=std::bit_cast<uint32_t>(40000.f);
        (void)decode_viewport_state(r,1280,720,mode==7?-1.f:1.f);
    }catch(const std::invalid_argument&){rejected=true;}require(rejected);}
    std::cout<<"GX2 viewport, HalfZ, resolution rounding, scissor, kill and invalid state PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
