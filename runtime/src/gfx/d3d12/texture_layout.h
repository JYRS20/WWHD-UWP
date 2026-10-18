#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace gfxd3d12 {
// One colour 2D base level. Arrays, depth, MSAA and mip chains need separate handling.
// Formats are GX2 values, not DXGI. Bytes remain in guest channel/block order.
// Pitch is in blocks; zero requests the address-library pitch. Other pitches must match it.
struct Texture2DLayout {
    uint32_t width{}, height{}, format{}, tile_mode{}, swizzle{}, pitch{};
};
struct LinearTexture2D {
    uint32_t width{}, height{}, format{}, row_bytes{}, block_rows{};
    std::vector<uint8_t> data;
};
// Throws on unsupported geometry/format or an address outside the provided image.
LinearTexture2D untile_texture_2d(const Texture2DLayout&, std::span<const uint8_t> image);
}
