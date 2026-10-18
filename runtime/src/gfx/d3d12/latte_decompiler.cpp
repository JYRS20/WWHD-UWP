// SPDX-License-Identifier: MPL-2.0
#include "latte_decompiler.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
namespace gfxd3d12 {
void select_latte_glsl() { g_renderer=std::make_unique<VulkanRenderer>(); }
}
