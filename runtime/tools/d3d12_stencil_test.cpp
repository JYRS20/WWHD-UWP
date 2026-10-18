#include "d3d12_output_test_fixture.h"
#include "gx2/gx2_regs.h"
#include <iostream>
#include <stdexcept>
using namespace gfxd3d12;
using namespace Latte;
static void require(bool b){if(!b)throw std::runtime_error("GX2 stencil CPU mismatch");}
static auto registers(uint32_t control,uint8_t reference,uint8_t read=255,uint8_t write=255,bool color=true) {
    std::vector<uint32_t> r(gx2::kNumRegs);
    r[REGADDR::DB_DEPTH_CONTROL]=1|control;r[REGADDR::CB_COLOR_CONTROL]=0xccu<<16;
    r[REGADDR::CB_TARGET_MASK]=color?15:0;
    r[REGADDR::DB_STENCILREFMASK]=reference|(uint32_t(read)<<8)|(uint32_t(write)<<16);
    r[REGADDR::DB_STENCILREFMASK_BF]=r[REGADDR::DB_STENCILREFMASK];return r;
}
static OutputMergerState stencil(uint32_t control,uint8_t ref,uint8_t read=255,uint8_t write=255,bool color=true) {
    auto r=registers(control,ref,read,write,color);return decode_output_merger_state(r,1,true,255,true);
}
static uint32_t front(uint32_t compare,uint32_t pass=0,uint32_t fail=0,uint32_t zfail=0) {
    return (compare<<8)|(fail<<11)|(pass<<14)|(zfail<<17);
}
int main(){try {
    const std::array<float,4> red{1,0,0,1},blue{0,0,1,1};std::vector<OutputCase> cases;
    auto check=[&](uint8_t value){return stencil(front(2),value);};
    // All 8 guest operations, including saturation and wrap at the endpoints.
    constexpr uint8_t initial[]{7,7,7,255,0,0x35,255,0},want[]{7,0,3,255,0,0xca,0,255};
    for(uint32_t op=0;op<8;++op){
        auto write=stencil(front(7,op),3,255,255,false);
        require(write.stencil_enabled&&write.stencil_front.pass==op&&write.stencil_back.pass==op);
        OutputCase c{"stencil operation",blue,1,{{write,red,.5f},{check(want[op]),red,.5f}},red};
        c.stencil_target=true;c.clear_stencil=initial[op];cases.push_back(c);
    }
    // Stencil compares reference OP stored value (ref=2, stored=3).
    constexpr bool pass[]{false,true,false,true,false,true,false,true};
    for(uint32_t func=0;func<8;++func){
        auto s=stencil(front(func),2);require(s.stencil_front.compare==func);
        OutputCase c{"stencil comparison",blue,1,{{s,red,.5f}},pass[func]?red:blue};
        c.stencil_target=true;c.clear_stencil=3;cases.push_back(c);
    }
    for(bool inverse:{false,true}){
        auto write=stencil(front(7,2),1,255,255,false);
        OutputStep mask{write,red,.5f};mask.scissor={0,0,16,32};
        auto read=stencil(front(inverse?5:2),1);
        OutputCase c{"spatial stencil mask",blue,1,{mask,{read,red,.5f}},inverse?red:blue};
        c.stencil_target=true;c.split_x=16;c.expected_left=inverse?blue:red;cases.push_back(c);
    }
    auto failed=stencil(front(0,0,2),5,255,255,false);
    OutputCase failCase{"stencil fail update",blue,1,{{failed,red,.5f},{check(5),red,.5f}},red};
    failCase.stencil_target=true;cases.push_back(failCase);
    auto zfailed=stencil(front(7,0,0,2)|2|(1<<4),5,255,255,false);
    OutputCase zfailCase{"depth fail stencil update",blue,.2f,{{zfailed,red,.8f},{check(5),red,.5f}},red};
    zfailCase.stencil_target=true;cases.push_back(zfailCase);
    auto masked=stencil(front(7,2),5,255,15,false);
    OutputCase writeMask{"stencil write mask",blue,1,{{masked,red,.5f},{check(0xa5),red,.5f}},red};
    writeMask.stencil_target=true;writeMask.clear_stencil=0xa0;cases.push_back(writeMask);
    auto read=stencil(front(2),5,15);
    OutputCase readMask{"stencil read mask",blue,1,{{read,red,.5f}},red};
    readMask.stencil_target=true;readMask.clear_stencil=0xa5;cases.push_back(readMask);
    for(bool reverse:{false,true}){
        // Clockwise authored triangle is front-facing under this decoded raster state.
        auto s=stencil(front(7,2)|(1<<7)|(7<<20)|(1u<<26),7,255,255,false);
        require(s.stencil_front.pass==2&&s.stencil_back.pass==1);
        RasterizerState raster{};raster.front_counter_clockwise=false;
        OutputCase c{"two-sided stencil operation",blue,1,{{s,red,.5f,raster,reverse},{check(7),red,.5f}},reverse?blue:red};
        c.stencil_target=true;c.clear_stencil=3;cases.push_back(c);
        auto differentFuncs=stencil(front(7)|(1<<7),7); // Back comparison defaults to NEVER.
        OutputCase f{"two-sided stencil comparison",blue,1,{{differentFuncs,red,.5f,raster,reverse}},reverse?blue:red};
        f.stencil_target=true;cases.push_back(f);
    }
    for(int bad=0;bad<5;++bad){auto r=registers(front(7)|(1<<7),7);bool rejected=false;try {
        if(bad==0)r[REGADDR::DB_STENCILREFMASK_BF]^=1;
        if(bad==1)r[REGADDR::DB_STENCILREFMASK_BF]^=1<<8;
        if(bad==2)r[REGADDR::DB_STENCILREFMASK_BF]^=1<<16;
        (void)decode_output_merger_state(r,1,bad!=4,255,bad!=3);
    }catch(const std::invalid_argument&){rejected=true;}require(rejected);}
    auto r=registers(front(7),1);r[REGADDR::DB_DEPTH_CONTROL]=0;
    require(!decode_output_merger_state(r,1,true,255,true).stencil_enabled);
    test_output_merger_gpu(cases);std::cout<<"GX2 stencil operations, comparisons, masks and face state PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
