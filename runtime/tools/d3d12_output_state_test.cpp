#include "d3d12_output_test_fixture.h"
#include "gx2/gx2_regs.h"
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace gfxd3d12;
using namespace Latte;
static void require(bool b){if(!b)throw std::runtime_error("GX2 output state CPU mismatch");}
static auto regs() {
    std::vector<uint32_t> r(gx2::kNumRegs);
    r[REGADDR::CB_COLOR_CONTROL]=0xccu<<16;r[REGADDR::CB_TARGET_MASK]=15;return r;
}
static OutputMergerState state(uint32_t depth=0,uint32_t blend=0,bool enable=false) {
    auto r=regs();r[REGADDR::DB_DEPTH_CONTROL]=depth;r[REGADDR::CB_BLEND0_CONTROL]=blend;
    if(enable)r[REGADDR::CB_COLOR_CONTROL]|=1<<8;return decode_output_merger_state(r,1,true);
}
int main(){try {
    std::vector<OutputCase> cases;
    auto less=state(2|4|(1<<4)),disabled=state(),readonly=state(2|(1<<4)),greater=state(2|4|(4<<4));
    require(less.depth_test&&less.depth_write&&less.depth_compare==1);
    require(!disabled.depth_test&&!disabled.depth_write&&!readonly.depth_write);
    cases.push_back({"less rejects far",{0,0,0,1},1,{{less,{1,0,0,1},.2f},{less,{0,0,1,1},.8f}},{1,0,0,1}});
    cases.push_back({"disabled depth",{0,0,0,1},1,{{disabled,{1,0,0,1},.2f},{disabled,{0,0,1,1},.8f}},{0,0,1,1}});
    cases.push_back({"read only depth",{0,0,0,1},1,{{readonly,{1,0,0,1},.2f},{readonly,{0,0,1,1},.8f}},{0,0,1,1}});
    cases.push_back({"greater rejects near",{0,0,0,1},0,{{greater,{1,0,0,1},.8f},{greater,{0,0,1,1},.2f}},{1,0,0,1}});
    // Separate alpha: RGB source-alpha / inverse-source-alpha, A source-one / dest-zero.
    auto alpha=state(0,4|(5<<8)|(1<<16)|(1u<<29),true);
    require(alpha.targets[0].source_alpha==1&&alpha.targets[0].destination_alpha==0);
    cases.push_back({"source alpha",{.25f,.5f,.75f,1},1,{{alpha,{1,0,0,.5f},.5f}},{.625f,.25f,.375f,.5f}});
    auto add=state(0,1|(1<<8),true);
    cases.push_back({"additive",{.2f,.3f,.4f,.5f},1,{{add,{.1f,.2f,.3f,.4f},.5f}},{.3f,.5f,.7f,.9f}});
    auto r=regs();r[REGADDR::CB_COLOR_CONTROL]|=1<<8;r[REGADDR::CB_BLEND0_CONTROL]=13;
    const uint32_t cr[]{REGADDR::CB_BLEND_RED,REGADDR::CB_BLEND_GREEN,REGADDR::CB_BLEND_BLUE,REGADDR::CB_BLEND_ALPHA};
    const std::array<float,4> constants{.2f,.4f,.6f,.8f};for(int i=0;i<4;++i)r[cr[i]]=std::bit_cast<uint32_t>(constants[i]);
    auto constant=decode_output_merger_state(r,1,true);
    cases.push_back({"constant blend",{0,0,0,0},1,{{constant,{1,1,1,1},.5f}},constants});
    r=regs();r[REGADDR::CB_TARGET_MASK]=5;auto mask=decode_output_merger_state(r,1,true);
    cases.push_back({"write mask",{.2f,.3f,.4f,.5f},1,{{mask,{1,0,0,1},.5f}},{1,.3f,0,.5f}});
    r=regs();r[REGADDR::CB_COLOR_CONTROL]|=1<<4;auto noColor=decode_output_merger_state(r,1,true);
    cases.push_back({"colour disabled",{.2f,.3f,.4f,.5f},1,{{noColor,{1,1,1,1},.5f}},{.2f,.3f,.4f,.5f}});
    const std::array<std::array<float,4>,4> wants{{{.5f,0,0,.4f},{.25f,.5f,.25f,.4f},{.75f,.5f,.75f,.8f},{0,0,.5f,0}}};
    for(uint32_t op=1;op<=4;++op) {
        auto blend=state(0,1|(1<<8)|(op<<5),true);
        cases.push_back({"blend operation",{.25f,.5f,.75f,.4f},1,{{blend,{.75f,.5f,.25f,.8f},.5f}},wants[op-1]});
    }
    for(uint32_t comparison=0;comparison<8;++comparison) require(state(2|(comparison<<4)).depth_compare==comparison);
    for(uint32_t factor=0;factor<=14;++factor) require(state(0,factor,true).targets[0].source_color==factor);
    r=regs();r[REGADDR::CB_TARGET_MASK]=0xa5;r[REGADDR::CB_COLOR_CONTROL]|=3<<8;
    auto mrt=decode_output_merger_state(r,2,false,1);
    require(mrt.targets[0].enabled&&!mrt.targets[1].enabled&&mrt.targets[0].write_mask==5&&mrt.targets[1].write_mask==10);
    for(int bad=0;bad<7;++bad){r=regs();bool rejected=false;try {
        if(bad==0)r.resize(3);
        if(bad==1)r[REGADDR::DB_DEPTH_CONTROL]=1;
        if(bad==2)r[REGADDR::CB_COLOR_CONTROL]=0x66<<16;
        if(bad==3){r[REGADDR::CB_COLOR_CONTROL]|=1<<8;r[REGADDR::CB_BLEND0_CONTROL]=19;}
        if(bad==4){r[REGADDR::CB_COLOR_CONTROL]|=1<<8;r[REGADDR::CB_BLEND0_CONTROL]=5<<5;}
        if(bad==5){r[REGADDR::CB_COLOR_CONTROL]|=1<<8;r[REGADDR::CB_BLEND0_CONTROL]=1<<13;}
        (void)decode_output_merger_state(r,bad==6?9:1,true);
    }catch(const std::invalid_argument&){rejected=true;}require(rejected);}
    test_output_merger_gpu(cases);
    std::cout<<"GX2 output state CPU checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
