#include "gfx/d3d12/draw_commands.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace gfxd3d12;
static void require(bool v) {if(!v) throw std::runtime_error("GX2 geometry test mismatch");}
int main() {try {
    const std::array<uint8_t,8> be16{0,1,0,2,0,3,0,4},le16{1,0,2,0,3,0,4,0};
    const std::array<uint8_t,16> be32{0,0,0,1,0,0,0,2,0,0,0,3,0,0,0,4},le32{1,0,0,0,2,0,0,0,3,0,0,0,4,0,0,0};
    for(uint32_t type : {0u,1u,4u,9u}) {
        std::span<const uint8_t> data=type==0?std::span<const uint8_t>(le16):type==1?std::span<const uint8_t>(le32):
            type==4?std::span<const uint8_t>(be16):std::span<const uint8_t>(be32);
        for(uint32_t prim : {1u,2u,3u,4u,5u,6u,0x12u,0x13u,0x14u}) {
            auto g=prepare_draw_geometry(prim,4,type,data,UINT32_MAX,2,true);
            std::vector<uint32_t> want{1,2,3,4};
            if(prim==5||prim==0x13) want={1,2,3,1,3,4};
            if(prim==0x14) want={1,2,3,2,4,3};
            if(prim==0x12) want={1,2,3,4,1};
            require(g.indices==want && g.count==want.size() && g.indexed && g.instances==2 && g.base_vertex==-1);
            require(pipeline_strip_cut(g)==D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED);
        }
        for(uint32_t prim:{3u,6u}) {
            auto g=prepare_draw_geometry(prim,4,type,data,0,1,true,true,2);
            require(g.indices==std::vector<uint32_t>{1,UINT32_MAX,3,4});
            require(pipeline_strip_cut(g)==D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF);
        }
        bool rejected=false;try{(void)prepare_draw_geometry(4,4,type,data.first(1),0,1,true);}catch(const std::out_of_range&){rejected=true;}
        require(rejected);
    }
    auto plain=prepare_draw_geometry(4,3,0,{},7,1,false);require(!plain.indexed && plain.count==3 && plain.base_vertex==7);
    auto fan=prepare_draw_geometry(5,4,0,{},0,1,false);require(fan.indices==std::vector<uint32_t>{0,1,2,0,2,3});
    auto quad=prepare_draw_geometry(0x13,4,0,{},0,1,false);require(quad.indices==fan.indices);
    require(pipeline_topology_type(quad.topology)==D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    for(uint32_t prim:{1u,2u,3u,4u,5u,6u,0x12u,0x13u,0x14u}) {
        require(prepare_draw_geometry(prim,0,4,{},0,1,true).count==0);
        require(prepare_draw_geometry(prim,3,4,{},0,0,true).count==0);
    }
    for(int kind=0;kind<5;++kind) {
        bool rejected=false;try {
            if(kind==0) (void)prepare_draw_geometry(99,3,0,{},0,1,false);
            if(kind==1) (void)prepare_draw_geometry(4,3,0,{},0,1,true);
            if(kind==2) (void)prepare_draw_geometry(4,3,3,be16,0,1,true);
            if(kind==3) (void)prepare_draw_geometry(4,3,4,be16,0,1,true,true,2);
            if(kind==4) (void)prepare_draw_geometry(4,0xffffffff,0,{},0,1,false);
        }catch(const std::exception&){rejected=true;}require(rejected);
    }
    std::cout<<"GX2 endian/topology expansion, base vertex, restart, zero draws and bounds PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
