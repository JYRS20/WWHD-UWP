// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "spirv_hlsl.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <winrt/base.h>
namespace gfxd3d12 {
struct StageLayout { ShaderStage stage; std::span<const ShaderBinding> bindings; };
struct DescriptorBinding { ShaderStage stage; ShaderBinding binding; uint32_t root_parameter, descriptor_offset; };
struct PipelineLayout {
    winrt::com_ptr<ID3D12RootSignature> signature;
    std::vector<DescriptorBinding> bindings;
    uint32_t descriptor_table_count = 0;
    bool compute = false;
};
// Descriptor tables still need descriptor heaps/resources populated by the eventual GX2 backend.
// Stage visibility keeps identical VS/PS register numbers independent; compute has its own layout.
PipelineLayout create_pipeline_layout(ID3D12Device* device,std::span<const StageLayout> shaders);
}
