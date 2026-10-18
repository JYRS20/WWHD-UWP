#include "texture_upload.h"
#include "bc45_decode.h"
#include <cstring>
#include <stdexcept>
namespace gfxd3d12 {
TextureUpload2D record_texture_upload_2d(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                                       const LinearTexture2D& source) {
    if (!device || !commands) throw std::invalid_argument("Missing texture upload device/commands");
    if ((source.format==0x34 || source.format==0x35) && ((source.width%4)||(source.height%4))) {
        auto expanded=decode_bc45_unorm(source);
        return record_texture_upload_2d(device,commands,expanded);
    }
    DXGI_FORMAT format;
    switch(source.format) {
    case 0x01: format=DXGI_FORMAT_R8_UNORM; break;
    case 0x07: format=DXGI_FORMAT_R8G8_UNORM; break;
    case 0x1a: format=DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case 0x41a: format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
    case 0x31: format=DXGI_FORMAT_BC1_UNORM; break;
    case 0x431: format=DXGI_FORMAT_BC1_UNORM_SRGB; break;
    case 0x32: format=DXGI_FORMAT_BC2_UNORM; break;
    case 0x432: format=DXGI_FORMAT_BC2_UNORM_SRGB; break;
    case 0x33: format=DXGI_FORMAT_BC3_UNORM; break;
    case 0x433: format=DXGI_FORMAT_BC3_UNORM_SRGB; break;
    case 0x34: format=DXGI_FORMAT_BC4_UNORM; break;
    case 0x234: format=DXGI_FORMAT_BC4_SNORM; break;
    case 0x35: format=DXGI_FORMAT_BC5_UNORM; break;
    case 0x235: format=DXGI_FORMAT_BC5_SNORM; break;
    default: throw std::invalid_argument("Unsupported upload format");
    }
    const bool bc=(source.format&0x3f)>=0x31;
    if (!source.width || !source.height || source.width>16384 || source.height>16384 ||
        (bc && ((source.width%4)||(source.height%4))))
        throw std::invalid_argument("Unsupported texture upload dimensions");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=source.width; desc.Height=source.height;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.Format=format; desc.SampleDesc.Count=1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows=0; UINT64 rowBytes=0,total=0;
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
    if (total==UINT64_MAX || rowBytes!=source.row_bytes || rows!=source.block_rows ||
        uint64_t(source.row_bytes)*rows!=source.data.size())
        throw std::invalid_argument("Invalid texture upload row layout");
    TextureUpload2D result;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    winrt::check_hresult(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),result.texture.put_void()));
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;
    buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    winrt::check_hresult(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),result.staging.put_void()));
    void* data=nullptr; D3D12_RANGE empty{0,0};
    winrt::check_hresult(result.staging->Map(0,&empty,&data));
    for(UINT y=0;y<rows;++y)
        std::memcpy(static_cast<uint8_t*>(data)+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,
            source.data.data()+size_t(y)*source.row_bytes,source.row_bytes);
    D3D12_RANGE written{0,size_t(total)};result.staging->Unmap(0,&written);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=result.staging.get();
    src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=result.texture.get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={result.texture.get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    commands->ResourceBarrier(1,&barrier);
    return result;
}
}
