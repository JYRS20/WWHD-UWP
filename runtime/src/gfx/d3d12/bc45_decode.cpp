#include "bc45_decode.h"
#include <stdexcept>
namespace gfxd3d12 {
LinearTexture2D decode_bc45_unorm(const LinearTexture2D& source) {
    if (source.format != 0x34 && source.format != 0x35)
        throw std::invalid_argument("BC fallback accepts BC4/BC5 UNORM only");
    const uint32_t channels=source.format==0x34?1:2;
    const uint32_t bw=(source.width+3)/4, bh=(source.height+3)/4;
    if (!source.width || !source.height || source.width>16384 || source.height>16384 ||
        source.row_bytes!=bw*channels*8 || source.block_rows!=bh ||
        source.data.size()!=uint64_t(source.row_bytes)*bh)
        throw std::invalid_argument("Invalid BC4/BC5 base-level layout");
    LinearTexture2D out{source.width,source.height,channels==1?0x01u:0x07u,
        source.width*channels,source.height,{}};
    out.data.resize(size_t(out.row_bytes)*out.height);
    for (uint32_t by=0;by<bh;++by) for(uint32_t bx=0;bx<bw;++bx)
        for(uint32_t c=0;c<channels;++c) {
            const auto* b=source.data.data()+size_t(by)*source.row_bytes+(size_t(bx)*channels+c)*8;
            uint32_t table[8]{b[0],b[1]};
            if (b[0]>b[1]) {
                for(uint32_t i=1;i<=6;++i) table[i+1]=((7-i)*b[0]+i*b[1]+3)/7;
            } else {
                for(uint32_t i=1;i<=4;++i) table[i+1]=((5-i)*b[0]+i*b[1]+2)/5;
                table[6]=0;table[7]=255;
            }
            uint64_t indices=0;
            for(uint32_t i=0;i<6;++i) indices|=uint64_t(b[2+i])<<(8*i);
            for(uint32_t y=0;y<4;++y) for(uint32_t x=0;x<4;++x) {
                const uint32_t px=bx*4+x,py=by*4+y;
                if(px<out.width && py<out.height)
                    out.data[size_t(py)*out.row_bytes+size_t(px)*channels+c]=
                        static_cast<uint8_t>(table[(indices>>(3*(y*4+x)))&7]);
            }
        }
    return out;
}
}
