// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "draw_geometry.h"
#include "viewport_state.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <winrt/base.h>
namespace gfxd3d12 {
// Caller binds compatible shaders, targets, buffers, descriptors, viewport and scissor.
// PSO topology type and strip-cut setting must match the prepared geometry.
// Keep returned upload storage alive until the draw fence completes.
winrt::com_ptr<ID3D12Resource> record_draw_geometry(ID3D12Device*,ID3D12GraphicsCommandList*,const DrawGeometry&);
// Returns false for a killed/empty draw; caller must then skip draw submission.
bool record_viewport_state(ID3D12GraphicsCommandList*,const ViewportState&);
D3D12_PRIMITIVE_TOPOLOGY_TYPE pipeline_topology_type(PrimitiveTopology);
D3D12_INDEX_BUFFER_STRIP_CUT_VALUE pipeline_strip_cut(const DrawGeometry&);
}
