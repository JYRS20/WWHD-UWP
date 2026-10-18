// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace gfxd3d12 {
enum class ShaderStage { Vertex, Fragment, Compute };
// One Vulkan resource can become two D3D12 bindings (combined texture and sampler).
struct ShaderBinding {
    uint32_t descriptor_set, binding;
    char register_type; // b/t/s/u
    uint32_t register_space, register_index, count;
};
struct HlslShader {
    std::string source;
    std::vector<ShaderBinding> bindings;
    uint32_t push_constant_bytes = 0;
};
// SM 5.1 translation only. No D3D12 device, root signature or executable backend is implied.
// Descriptor registers are densely allocated per register type/space, so Vulkan descriptor arrays
// cannot overlap neighbouring HLSL registers. Callers must use bindings, not assume identical indices.
// Push constants are mapped to b0, space32. Descriptor sets >=32 are explicitly rejected.
// Throws on invalid/unsupported SPIR-V instead of silently substituting shader behaviour.
HlslShader spirv_to_hlsl(std::span<const uint32_t> words, ShaderStage stage);
}
