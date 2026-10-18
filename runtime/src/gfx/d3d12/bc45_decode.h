#pragma once
#include "texture_layout.h"
namespace gfxd3d12 {
// Expand BC4/BC5 UNORM and crop to the logical extent. Interpolants round to 8-bit UNORM.
// Keeps native R/RG channel semantics, including zero missing channels and alpha one.
LinearTexture2D decode_bc45_unorm(const LinearTexture2D&);
}
