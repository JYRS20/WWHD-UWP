#include "d3d12_output_test_fixture.h"
#include "gx2/gx2_regs.h"
#include <iostream>
#include <stdexcept>
using namespace gfxd3d12;
using namespace Latte;
static void require(bool b){if(!b)throw std::runtime_error("GX2 rasterizer CPU validation failed");}
int main(){try {
    std::vector<uint32_t> r(gx2::kNumRegs);std::vector<OutputCase> cases;
    r[REGADDR::CB_COLOR_CONTROL]=0xccu<<16;r[REGADDR::CB_TARGET_MASK]=15;
    auto output=decode_output_merger_state(r,1,false);
    const std::array<float,4> red{1,0,0,1},blue{0,0,1,1};
    for(bool cw:{false,true})for(bool reverse:{false,true})for(uint32_t mask=0;mask<4;++mask){
        r[REGADDR::PA_SU_SC_MODE_CNTL]=mask|(cw?4:0);auto raster=decode_rasterizer_state(r);
        require(raster.cull_front==bool(mask&1)&&raster.cull_back==bool(mask&2)&&raster.front_counter_clockwise==!cw);
        require(raster.skip==(mask==3));
        // Authored clip-space triangle (-1,-1),(-1,3),(3,-1) is visually clockwise.
        // Positive-height D3D viewport preserves that visual classification; swapping 1/2 reverses it.
        bool front=cw!=reverse,culled=front?bool(mask&1):bool(mask&2);
        cases.push_back({"winding/cull",blue,1,{{output,red,.5f,raster,reverse}},culled?blue:red});
    }
    r[REGADDR::PA_SU_SC_MODE_CNTL]=0;
    for(bool noZClip:{false,true})for(float z:{-.5f,1.5f}){
        r[REGADDR::PA_CL_CLIP_CNTL]=noZClip?((1u<<26)|(1u<<27)):0;
        auto raster=decode_rasterizer_state(r);require(raster.depth_clip==!noZClip);
        cases.push_back({"symmetric Z clip",blue,1,{{output,red,z,raster,false}},noZClip?red:blue});
    }
    r[REGADDR::PA_CL_CLIP_CNTL]=1u<<22;auto killed=decode_rasterizer_state(r);require(killed.skip);
    cases.push_back({"rasterization kill",blue,1,{{output,red,.5f,killed,false}},blue});
    for(int bad=0;bad<7;++bad){r.assign(gx2::kNumRegs,0);bool rejected=false;try {
        if(bad==0)r.resize(3);
        if(bad==1)r[REGADDR::PA_SU_SC_MODE_CNTL]=1<<3;
        if(bad==2)r[REGADDR::PA_SU_SC_MODE_CNTL]=1<<11;
        if(bad==3)r[REGADDR::PA_SU_SC_MODE_CNTL]=1<<12;
        if(bad==4)r[REGADDR::PA_SU_SC_MODE_CNTL]=1<<13;
        if(bad==5)r[REGADDR::PA_CL_CLIP_CNTL]=1u<<26;
        if(bad==6)r[REGADDR::PA_CL_CLIP_CNTL]=1u<<16;
        (void)decode_rasterizer_state(r);
    }catch(const std::invalid_argument&){rejected=true;}require(rejected);}
    test_output_merger_gpu(cases);std::cout<<"GX2 rasterizer winding/cull/clip CPU and GPU checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
