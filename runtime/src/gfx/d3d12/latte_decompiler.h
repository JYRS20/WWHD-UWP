// SPDX-License-Identifier: MPL-2.0
#pragma once
namespace gfxd3d12 {
// PlatformBackend::select_decompiler callback: selects Cemu's Vulkan-style GLSL output only.
// No Vulkan device, loader or Vulkan backend is required by this flavour shim.
void select_latte_glsl();
}
