#include "gfx/d3d12/texture_layout.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace gfxd3d12;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static uint32_t reference_offset(uint32_t x, uint32_t y, uint32_t bytes,
    const LatteAddrLib::AddrSurfaceInfo_OUT& info, uint32_t pitch, uint32_t swizzle) {
    const auto tm = info.hwTileMode;
    if (tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
        return LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x,y,0,0,bytes*8,pitch,info.height,1);
    if (!Latte::TM_IsMacroTiled(tm))
        return LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x,y,0,bytes*8,pitch,info.height,tm,false);
    // Deliberately use the uncached address implementation as an independent check of the cache.
    return LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled(x,y,0,0,bytes*8,pitch,info.height,1,tm,false,
        (swizzle>>8)&1,(swizzle>>9)&3);
}
static void round_trip(uint32_t fmt, uint32_t bytes, uint32_t block, uint32_t tile, uint32_t swizzle, bool explicit_pitch) {
    Texture2DLayout d{101,59,fmt,tile,swizzle,0};
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    LatteAddrLib::GX2CalculateSurfaceInfo(static_cast<Latte::E_GX2SURFFMT>(fmt),d.width,d.height,1,
        Latte::E_DIM::DIM_2D, static_cast<Latte::E_GX2TILEMODE>(tile),0,0,&info);
    if (explicit_pitch) d.pitch = info.pitch;
    const uint32_t bw=(d.width+block-1)/block,bh=(d.height+block-1)/block;
    std::vector<uint8_t> tiled(static_cast<size_t>(info.surfSize),0xcc), expected(size_t(bw)*bh*bytes);
    for (uint32_t y=0;y<bh;++y) for (uint32_t x=0;x<bw;++x) {
        const auto off=reference_offset(x,y,bytes,info,info.pitch,swizzle);
        require(size_t(off)+bytes<=tiled.size(),"Reference offset outside allocation");
        for (uint32_t b=0;b<bytes;++b) {
            uint8_t v=static_cast<uint8_t>((x*17+y*41+b*67)^(x>>3));
            tiled[off+b]=v; expected[(size_t(y)*bw+x)*bytes+b]=v;
        }
    }
    auto got=untile_texture_2d(d,tiled);
    require(got.data==expected && got.row_bytes==bw*bytes && got.block_rows==bh,"Untile mismatch");
    bool rejected=false;
    try { (void)untile_texture_2d(d,{}); } catch (const std::out_of_range&) { rejected=true; }
    require(rejected,"Truncated input accepted");
}
static uint32_t be(std::span<const uint8_t> b, size_t p, size_t n) {
    require(p+n<=b.size(),"Truncated BFLIM header"); uint32_t v=0;
    for (size_t i=0;i<n;++i) v=(v<<8)|b[p+i]; return v;
}
static void real_bflim(const char* path) {
    std::ifstream in(path,std::ios::binary); require(bool(in),"BFLIM input unavailable");
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),{});
    require(b.size()>=40,"BFLIM too small"); size_t p=b.size()-40;
    require(be(b,p,4)==0x464c494d && be(b,p+4,2)==0xfeff && be(b,p+20,4)==0x696d6167,"Unsupported BFLIM footer");
    require(be(b,p+12,4)==b.size() && be(b,p+36,4)==p,"BFLIM size mismatch");
    uint32_t fmt;
    switch(b[p+34]) {
    case 9: fmt=0x1a; break; case 12: fmt=0x31; break; case 13: fmt=0x32; break;
    case 14: fmt=0x33; break; case 15: case 16: fmt=0x34; break; case 17: fmt=0x35; break;
    default: throw std::runtime_error("Unsupported BFLIM fixture format");
    }
    Texture2DLayout d{be(b,p+28,2),be(b,p+30,2),fmt,uint32_t(b[p+35]&31),uint32_t(b[p+35]>>5)<<8,0};
    auto out=untile_texture_2d(d,std::span<const uint8_t>(b.data(),p));
    require(!out.data.empty(),"Empty texture");
    // Source data remains private. No game-derived output is written by this test.
    std::cout << "BFLIM " << d.width << 'x' << d.height << " GX2 format=" << fmt
        << " tile=" << d.tile_mode << " linear_bytes=" << out.data.size() << '\n';
}
int main(int argc,char** argv) { try {
    unsigned cases=0;
    for (uint32_t tile : {0u,1u,2u,4u}) for (uint32_t swizzle : {0u,0x100u,0x700u})
      for (bool pitch : {false,true}) {
        round_trip(0x1a,4,1,tile,swizzle,pitch); ++cases;
        for (uint32_t fmt : {0x31u,0x32u,0x33u,0x34u,0x35u}) {
            round_trip(fmt,(fmt==0x31||fmt==0x34)?8:16,4,tile,swizzle,pitch); ++cases;
        }
    }
    for (auto d : {Texture2DLayout{},Texture2DLayout{8,8,0xffff,2},Texture2DLayout{8,8,0x1a,3},
                   Texture2DLayout{8,8,0x1a,4,0,0xffffffff}}) {
        bool rejected=false; try { (void)untile_texture_2d(d,{}); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Invalid descriptor accepted");
    }
    for(int i=1;i<argc;++i) real_bflim(argv[i]);
    std::cout << cases << " patterned texture cases and invalid inputs passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
