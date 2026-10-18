#include "d3d12_output_test_fixture.h"
#include "gfx/d3d12/output_merger_commands.h"
#include "gfx/d3d12/rasterizer_commands.h"
#include <winrt/base.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
winrt::com_ptr<ID3D12Resource> resource(ID3D12Device* d,const D3D12_RESOURCE_DESC& desc,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear=nullptr){
    D3D12_HEAP_PROPERTIES h{};h.Type=type;winrt::com_ptr<ID3D12Resource> r;
    winrt::check_hresult(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,state,clear,__uuidof(ID3D12Resource),r.put_void()));return r;
}
winrt::com_ptr<ID3DBlob> compile(const char* source,const char* entry,const char* profile){
    winrt::com_ptr<ID3DBlob> code,error;auto hr=D3DCompile(source,std::strlen(source),"authored-output-test",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,code.put(),error.put());
    if(FAILED(hr)&&error)std::cerr.write(static_cast<char*>(error->GetBufferPointer()),error->GetBufferSize());winrt::check_hresult(hr);return code;
}
}
void test_output_merger_gpu(const std::vector<OutputCase>& cases){
    using namespace gfxd3d12;
    winrt::com_ptr<IDXGIFactory4> factory;winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory4),factory.put_void()));
    winrt::com_ptr<IDXGIAdapter> warp;winrt::check_hresult(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),warp.put_void()));
    winrt::com_ptr<ID3D12Device> d;winrt::check_hresult(D3D12CreateDevice(warp.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),d.put_void()));
    const char* shader=R"(
cbuffer Values:register(b0){float4 color;float depth;float reverse;};
float4 vs(uint id:SV_VertexID):SV_Position {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};if(reverse>0.5 && id>0)id=3-id;return float4(p[id],depth,1);}
float4 ps():SV_Target {return color;})";
    auto vs=compile(shader,"vs","vs_5_1"),ps=compile(shader,"ps","ps_5_1");
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameter.Constants={0,0,6};
    D3D12_ROOT_SIGNATURE_DESC root{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};winrt::com_ptr<ID3DBlob> serial;
    winrt::check_hresult(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,serial.put(),nullptr));
    winrt::com_ptr<ID3D12RootSignature> signature;winrt::check_hresult(d->CreateRootSignature(0,serial->GetBufferPointer(),serial->GetBufferSize(),__uuidof(ID3D12RootSignature),signature.put_void()));
    D3D12_COMMAND_QUEUE_DESC q{};winrt::com_ptr<ID3D12CommandQueue> queue;winrt::check_hresult(d->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.put_void()));
    for(const auto& c:cases){
        D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=image.Height=32;
        image.DepthOrArraySize=image.MipLevels=1;image.SampleDesc.Count=1;image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};clear.Format=image.Format;std::memcpy(clear.Color,c.clear.data(),16);
        auto target=resource(d.get(),image,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear);
        auto depthDesc=image;depthDesc.Format=c.stencil_target?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT;depthDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE zclear{};zclear.Format=depthDesc.Format;zclear.DepthStencil.Depth=c.clear_depth;zclear.DepthStencil.Stencil=c.clear_stencil;
        auto depth=resource(d.get(),depthDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,&zclear);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        winrt::com_ptr<ID3D12DescriptorHeap> rtv,dsv;winrt::check_hresult(d->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),rtv.put_void()));
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;winrt::check_hresult(d->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),dsv.put_void()));
        auto rh=rtv->GetCPUDescriptorHandleForHeapStart(),dh=dsv->GetCPUDescriptorHandleForHeapStart();
        d->CreateRenderTargetView(target.get(),nullptr,rh);d->CreateDepthStencilView(depth.get(),nullptr,dh);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;d->GetCopyableFootprints(&image,0,1,0,&footprint,nullptr,nullptr,&total);
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto readback=resource(d.get(),bd,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        winrt::com_ptr<ID3D12CommandAllocator> allocator;winrt::check_hresult(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.put_void()));
        winrt::com_ptr<ID3D12GraphicsCommandList> cmd;winrt::check_hresult(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.get(),nullptr,__uuidof(ID3D12GraphicsCommandList),cmd.put_void()));
        cmd->OMSetRenderTargets(1,&rh,FALSE,&dh);cmd->ClearRenderTargetView(rh,c.clear.data(),0,nullptr);cmd->ClearDepthStencilView(dh,c.stencil_target?D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL:D3D12_CLEAR_FLAG_DEPTH,c.clear_depth,c.clear_stencil,0,nullptr);
        D3D12_VIEWPORT viewport{0,0,32,32,0,1};D3D12_RECT scissor{0,0,32,32};cmd->RSSetViewports(1,&viewport);cmd->RSSetScissorRects(1,&scissor);
        cmd->SetGraphicsRootSignature(signature.get());cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        std::vector<winrt::com_ptr<ID3D12PipelineState>> pipelines;
        for(const auto& step:c.steps){
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=signature.get();desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
            const bool submit=apply_rasterizer_state(desc,step.raster);
            D3D12_RECT stepScissor{step.scissor[0],step.scissor[1],step.scissor[2],step.scissor[3]};cmd->RSSetScissorRects(1,&stepScissor);
            desc.SampleMask=UINT_MAX;desc.SampleDesc.Count=1;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets=1;desc.RTVFormats[0]=image.Format;desc.DSVFormat=depthDesc.Format;
            apply_output_merger_state(desc,step.state);
            winrt::com_ptr<ID3D12PipelineState> pipeline;winrt::check_hresult(d->CreateGraphicsPipelineState(&desc,__uuidof(ID3D12PipelineState),pipeline.put_void()));
            cmd->SetPipelineState(pipeline.get());pipelines.push_back(std::move(pipeline));record_output_merger_constants(cmd.get(),step.state);
            std::array<float,6> values{step.color[0],step.color[1],step.color[2],step.color[3],step.depth,step.reverse_winding?1.f:0.f};cmd->SetGraphicsRoot32BitConstants(0,6,values.data(),0);if(submit)cmd->DrawInstanced(3,1,0,0);
        }
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={target.get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};cmd->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=target.get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        winrt::check_hresult(cmd->Close());ID3D12CommandList* lists[]{cmd.get()};queue->ExecuteCommandLists(1,lists);
        winrt::com_ptr<ID3D12Fence> fence;winrt::check_hresult(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.put_void()));
        winrt::handle event{CreateEventExW(nullptr,nullptr,0,EVENT_ALL_ACCESS)};require(bool(event),"Output fence event unavailable");
        winrt::check_hresult(queue->Signal(fence.get(),1));winrt::check_hresult(fence->SetEventOnCompletion(1,event.get()));require(WaitForSingleObjectEx(event.get(),10000,FALSE)==WAIT_OBJECT_0,"Output fence timeout");
        void* mapped=nullptr;D3D12_RANGE all{0,size_t(total)};winrt::check_hresult(readback->Map(0,&all,&mapped));bool matches=true;
        for(UINT y=0;y<32;++y)for(UINT x=0;x<32;++x)for(UINT k=0;k<4;++k){
            const uint8_t actual=static_cast<uint8_t*>(mapped)[footprint.Offset+y*footprint.Footprint.RowPitch+x*4+k];
            const auto& expected=c.split_x>=0&&int32_t(x)<c.split_x?c.expected_left:c.expected;
            matches&=std::abs(int(actual)-int(std::lround(expected[k]*255)))<=1;
        }
        D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);
        if(!matches)std::cerr<<"Unexpected output pixels: "<<c.name<<'\n';require(matches,"GX2 pipeline GPU validation failed");
    }
    std::cout<<cases.size()<<" pipeline GPU cases passed (WARP), all 32x32 pixels checked\n";
}
