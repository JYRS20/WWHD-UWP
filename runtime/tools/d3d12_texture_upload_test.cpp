#include "gfx/d3d12/texture_upload.h"
#include <dxgi1_4.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace gfxd3d12;
static void require(bool v,const char* message) { if(!v) throw std::runtime_error(message); }
int main() { try {
    winrt::com_ptr<IDXGIFactory4> factory;
    winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory4),factory.put_void()));
    winrt::com_ptr<IDXGIAdapter> warp;
    winrt::check_hresult(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),warp.put_void()));
    winrt::com_ptr<ID3D12Device> device;
    winrt::check_hresult(D3D12CreateDevice(warp.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),device.put_void()));
    winrt::com_ptr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC q{};
    winrt::check_hresult(device->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.put_void()));
    unsigned count=0;
    for(uint32_t fmt : {0x1au,0x41au,0x31u,0x431u,0x32u,0x432u,0x33u,0x433u,0x34u,0x234u,0x35u,0x235u}) {
        bool bc=(fmt&63)>=0x31; uint32_t bytes=bc?((fmt&63)==0x31||(fmt&63)==0x34?8:16):4;
        LinearTexture2D input{104,60,fmt,(bc?26u:104u)*bytes,bc?15u:60u,{}};
        input.data.resize(size_t(input.row_bytes)*input.block_rows);
        for(size_t i=0;i<input.data.size();++i) input.data[i]=uint8_t((i*37)^(i>>8));
        winrt::com_ptr<ID3D12CommandAllocator> allocator;
        winrt::check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.put_void()));
        winrt::com_ptr<ID3D12GraphicsCommandList> commands;
        winrt::check_hresult(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.get(),nullptr,__uuidof(ID3D12GraphicsCommandList),commands.put_void()));
        auto uploaded=record_texture_upload_2d(device.get(),commands.get(),input);
        auto desc=uploaded.texture->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total;
        device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;
        buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        winrt::com_ptr<ID3D12Resource> readback;
        winrt::check_hresult(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,__uuidof(ID3D12Resource),readback.put_void()));
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={uploaded.texture.get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
        commands->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=uploaded.texture.get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        // Invalid inputs must fail before recording a copy or creating resources.
        for(int bad=0;bad<3;++bad) {
            auto invalid=input;
            if(bad==0) invalid.data.pop_back();
            if(bad==1) invalid.row_bytes++;
            if(bad==2) invalid.format=0xffff;
            bool rejected=false;
            try { (void)record_texture_upload_2d(device.get(),commands.get(),invalid); }
            catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Malformed upload accepted");
        }
        if(bc && fmt!=0x34 && fmt!=0x35) {
            auto invalid=input; invalid.width--;
            bool rejected=false;
            try { (void)record_texture_upload_2d(device.get(),commands.get(),invalid); }
            catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Unaligned BC base dimensions accepted");
        }
        winrt::check_hresult(commands->Close());ID3D12CommandList* lists[]{commands.get()};queue->ExecuteCommandLists(1,lists);
        winrt::com_ptr<ID3D12Fence> fence;
        winrt::check_hresult(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.put_void()));
        winrt::handle event{CreateEventExW(nullptr,nullptr,0,EVENT_ALL_ACCESS)};require(bool(event),"Fence event unavailable");
        winrt::check_hresult(queue->Signal(fence.get(),1));winrt::check_hresult(fence->SetEventOnCompletion(1,event.get()));
        require(WaitForSingleObjectEx(event.get(),10000,FALSE)==WAIT_OBJECT_0,"Texture upload fence timeout");
        void* data=nullptr;D3D12_RANGE range{0,size_t(total)};
        winrt::check_hresult(readback->Map(0,&range,&data));
        bool matches=true;
        for(UINT y=0;y<input.block_rows;++y)
            matches &= std::memcmp(static_cast<uint8_t*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,
                input.data.data()+size_t(y)*input.row_bytes,input.row_bytes)==0;
        D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);
        require(matches,"D3D12 texture upload/readback mismatch");++count;
    }
    std::cout<<count<<" D3D12 WARP texture formats uploaded and read back exactly; invalid layouts rejected\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
