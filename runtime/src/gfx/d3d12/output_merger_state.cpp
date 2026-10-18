// SPDX-License-Identifier: MPL-2.0
#include "output_merger_state.h"
#include "gx2/gx2_regs.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace gfxd3d12 {
OutputMergerState decode_output_merger_state(std::span<const uint32_t> r,uint32_t count,bool depth,uint8_t blendable,bool has_stencil) {
    using namespace Latte;
    if(r.size()<=REGADDR::CB_COLOR_CONTROL || count>8 || (has_stencil&&!depth))
        throw std::invalid_argument("Invalid GX2 output-merger input");
    OutputMergerState s{};s.target_count=count;
    LATTE_DB_DEPTH_CONTROL dc{};std::memcpy(&dc,&r[REGADDR::DB_DEPTH_CONTROL],4);
    if(depth && dc.get_STENCIL_ENABLE() && !has_stencil)
        throw std::invalid_argument("GX2 stencil requires a stencil-capable attachment");
    s.stencil_enabled=depth&&has_stencil&&dc.get_STENCIL_ENABLE();
    if(s.stencil_enabled) {
        const uint32_t front=r[REGADDR::DB_STENCILREFMASK],back=r[REGADDR::DB_STENCILREFMASK_BF];
        if(dc.get_BACK_STENCIL_ENABLE() && ((front^back)&0xffffff))
            throw std::invalid_argument("Different GX2 face stencil reference/masks require separate handling");
        s.stencil_reference=front&255;s.stencil_read_mask=(front>>8)&255;s.stencil_write_mask=(front>>16)&255;
        s.stencil_front={static_cast<uint32_t>(dc.get_STENCIL_FUNC_F()),static_cast<uint32_t>(dc.get_STENCIL_FAIL_F()),
            static_cast<uint32_t>(dc.get_STENCIL_ZFAIL_F()),static_cast<uint32_t>(dc.get_STENCIL_ZPASS_F())};
        s.stencil_back=s.stencil_front;
        if(dc.get_BACK_STENCIL_ENABLE()) s.stencil_back={static_cast<uint32_t>(dc.get_STENCIL_FUNC_B()),
            static_cast<uint32_t>(dc.get_STENCIL_FAIL_B()),static_cast<uint32_t>(dc.get_STENCIL_ZFAIL_B()),
            static_cast<uint32_t>(dc.get_STENCIL_ZPASS_B())};
    }
    s.depth_test=depth&&dc.get_Z_ENABLE();s.depth_write=s.depth_test&&dc.get_Z_WRITE_ENABLE();
    s.depth_compare=static_cast<uint32_t>(dc.get_Z_FUNC());
    const uint32_t control=r[REGADDR::CB_COLOR_CONTROL];
    const uint32_t special=(control>>4)&7,rop=(control>>16)&255;
    if(count && (special>1 || rop!=0xcc))
        throw std::invalid_argument("GX2 special colour/logic operation is unsupported");
    const uint32_t constantRegs[]{REGADDR::CB_BLEND_RED,REGADDR::CB_BLEND_GREEN,REGADDR::CB_BLEND_BLUE,REGADDR::CB_BLEND_ALPHA};
    for(uint32_t i=0;i<4;++i) {
        s.blend_constant[i]=std::bit_cast<float>(r[constantRegs[i]]);
        if(!std::isfinite(s.blend_constant[i])) throw std::invalid_argument("Non-finite GX2 blend constant");
    }
    for(uint32_t i=0;i<count;++i) {
        auto& b=s.targets[i];b.write_mask=special==1?0:uint8_t((r[REGADDR::CB_TARGET_MASK]>>(i*4))&15);
        b.enabled=((control>>(8+i))&1)&&((blendable>>i)&1);
        if(!b.enabled) continue;
        LATTE_CB_BLENDN_CONTROL raw{};std::memcpy(&raw,&r[REGADDR::CB_BLEND0_CONTROL+i],4);
        if(raw.get_OPACITY_WEIGHT()) throw std::invalid_argument("GX2 opacity weighting is unsupported");
        b.source_color=static_cast<uint32_t>(raw.get_COLOR_SRCBLEND());
        b.destination_color=static_cast<uint32_t>(raw.get_COLOR_DSTBLEND());
        b.color_operation=static_cast<uint32_t>(raw.get_COLOR_COMB_FCN());
        b.source_alpha=raw.get_SEPARATE_ALPHA_BLEND()?static_cast<uint32_t>(raw.get_ALPHA_SRCBLEND()):b.source_color;
        b.destination_alpha=raw.get_SEPARATE_ALPHA_BLEND()?static_cast<uint32_t>(raw.get_ALPHA_DSTBLEND()):b.destination_color;
        b.alpha_operation=raw.get_SEPARATE_ALPHA_BLEND()?static_cast<uint32_t>(raw.get_ALPHA_COMB_FCN()):b.color_operation;
        if(b.source_color>14 || b.destination_color>14 || b.source_alpha>14 || b.destination_alpha>14 ||
           b.color_operation>4 || b.alpha_operation>4)
            throw std::invalid_argument("Unsupported GX2 blend factor/operation");
    }
    return s;
}
}
