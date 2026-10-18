// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "rasterizer_state.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
namespace gfxd3d12 {
// Returns false when the caller MUST omit draw submission (kill or both faces culled).
// Include decoded state in the PSO key. No nonstandard viewport orientation is implied.
bool apply_rasterizer_state(D3D12_GRAPHICS_PIPELINE_STATE_DESC&,const RasterizerState&);
}
