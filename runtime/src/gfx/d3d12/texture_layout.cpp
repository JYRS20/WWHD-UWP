#include "texture_layout.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include <cstring>
#include <stdexcept>

namespace gfxd3d12 {
LinearTexture2D untile_texture_2d(const Texture2DLayout& d, std::span<const uint8_t> image) {
    // Restrict to layouts tested here, before calling the guest address library.
    if (!d.width || !d.height || d.width > 16384 || d.height > 16384)
        throw std::invalid_argument("Invalid 2D texture dimensions");
    if (d.tile_mode > 4 || d.tile_mode == 3)
        throw std::invalid_argument("Unsupported 2D texture tile mode");
    uint32_t bytes = 0, block = 1;
    switch (d.format) {
    case 0x1a: case 0x41a: bytes = 4; break; // RGBA8, sRGB
    case 0x31: case 0x431: case 0x34: case 0x234: bytes = 8; block = 4; break;
    case 0x32: case 0x432: case 0x33: case 0x433:
    case 0x35: case 0x235: bytes = 16; block = 4; break;
    default: throw std::invalid_argument("Unsupported 2D texture format");
    }
    const uint32_t bw = (d.width + block - 1) / block;
    const uint32_t bh = (d.height + block - 1) / block;
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    LatteAddrLib::GX2CalculateSurfaceInfo(static_cast<Latte::E_GX2SURFFMT>(d.format),
        d.width, d.height, 1, Latte::E_DIM::DIM_2D,
        static_cast<Latte::E_GX2TILEMODE>(d.tile_mode), 0, 0, &info);
    const uint32_t pitch = d.pitch ? d.pitch : info.pitch;
    // Limit caller-controlled arithmetic to the address library's 32-bit range.
    if ((d.pitch && d.pitch != info.pitch) || pitch < bw || pitch > 65536 || !info.height ||
        uint64_t(pitch) * info.height * bytes > UINT32_MAX)
        throw std::invalid_argument("Invalid 2D texture pitch");
    const auto tm = info.hwTileMode;
    const bool macro = Latte::TM_IsMacroTiled(tm);
    const uint32_t pipe = (d.swizzle >> 8) & 1, bank = (d.swizzle >> 9) & 3;
    LatteAddrLib::CachedSurfaceAddrInfo cached{};
    if (macro) LatteAddrLib::SetupCachedSurfaceAddrInfo(&cached, 0, 0, bytes * 8,
        pitch, info.height, 1, 1, tm, false, pipe, bank);
    LinearTexture2D out{d.width, d.height, d.format, bw * bytes, bh, {}};
    out.data.resize(size_t(out.row_bytes) * bh);
    for (uint32_t y = 0; y < bh; ++y) for (uint32_t x = 0; x < bw; ++x) {
        uint32_t offset;
        if (tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
            offset = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x,y,0,0,bytes*8,pitch,info.height,1);
        else if (!macro)
            offset = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x,y,0,bytes*8,pitch,info.height,tm,false);
        else offset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x,y,&cached);
        if (offset > image.size() || bytes > image.size() - offset)
            throw std::out_of_range("Texture image is truncated or its layout is invalid");
        std::memcpy(out.data.data() + size_t(y)*out.row_bytes + size_t(x)*bytes,
                    image.data() + offset, bytes);
    }
    return out;
}
}
