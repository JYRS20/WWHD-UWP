// Test-only draw of an authored triangle using the owner's primitive-renderer shader pair.
#pragma once
#include "gfx/d3d12/spirv_hlsl.h"
#include <span>
#include <filesystem>
void draw_primitive_shader_test(const gfxd3d12::HlslShader& vs,std::span<const uint8_t> vsCode,
 const gfxd3d12::HlslShader& ps,std::span<const uint8_t> psCode,const std::filesystem::path& output,std::span<const uint32_t> registers,bool indexed=false,bool clipped=false);
