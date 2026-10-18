#pragma once
#include "gfx/d3d12/output_merger_state.h"
#include <vector>
#include "gfx/d3d12/rasterizer_state.h"
struct OutputStep {gfxd3d12::OutputMergerState state;std::array<float,4> color;float depth;gfxd3d12::RasterizerState raster{};bool reverse_winding{};std::array<int32_t,4> scissor{0,0,32,32};};
struct OutputCase {const char* name;std::array<float,4> clear;float clear_depth{1};
    std::vector<OutputStep> steps;std::array<float,4> expected;bool stencil_target{};uint8_t clear_stencil{};int32_t split_x{-1};std::array<float,4> expected_left{};};
void test_output_merger_gpu(const std::vector<OutputCase>&);
