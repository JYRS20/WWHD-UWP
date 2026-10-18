#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "texture_layout.h"
#include <d3d12.h>
#include <winrt/base.h>
namespace gfxd3d12 {
struct TextureUpload2D {
    winrt::com_ptr<ID3D12Resource> texture;
    // Caller must retain this allocation until the submitted copy's GPU fence completes.
    winrt::com_ptr<ID3D12Resource> staging;
};
// Records a single base-level upload and transition to shader-readable state.
// Unaligned BC4/BC5 UNORM expands to cropped R8/RG8, preserving logical UV extent.
// Other BC base dimensions must be multiples of four. No implicit UV correction.
TextureUpload2D record_texture_upload_2d(ID3D12Device*, ID3D12GraphicsCommandList*, const LinearTexture2D&);
}
