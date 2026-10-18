// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "output_merger_state.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
namespace gfxd3d12 {
// Does not supply formats, render targets or shaders. Include decoded state in the PSO cache key.
void apply_output_merger_state(D3D12_GRAPHICS_PIPELINE_STATE_DESC&,const OutputMergerState&);
void record_output_merger_constants(ID3D12GraphicsCommandList*,const OutputMergerState&);
}
