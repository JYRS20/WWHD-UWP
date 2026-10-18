#include "gfx/d3d12/descriptor_snapshot.h"
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace gfxd3d12;
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid descriptor operation accepted");}
static auto buffer(ID3D12Device* d,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES h{};h.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=16;desc.Height=1;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(type==D3D12_HEAP_TYPE_DEFAULT)desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    winrt::com_ptr<ID3D12Resource> r;winrt::check_hresult(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,__uuidof(ID3D12Resource),r.put_void()));return r;
}
int main(){try {
    winrt::com_ptr<IDXGIFactory4> factory;winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory4),factory.put_void()));
    winrt::com_ptr<IDXGIAdapter> warp;winrt::check_hresult(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),warp.put_void()));
    winrt::com_ptr<ID3D12Device> device;winrt::check_hresult(D3D12CreateDevice(warp.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),device.put_void()));
    const char* shader=R"(
struct Payload {uint4 data;uint4 padding;};
ConstantBuffer<Payload> inputs[2] : register(b0);
RWStructuredBuffer<uint4> result : register(u0);
[numthreads(1,1,1)] void main(){result[0]=inputs[0].data+inputs[1].data+inputs[0].padding+inputs[1].padding;})";
    winrt::com_ptr<ID3DBlob> code,error;auto hr=D3DCompile(shader,std::strlen(shader),"authored-descriptor-fixture",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,code.put(),error.put());
    if(FAILED(hr)&&error)std::cerr.write(static_cast<char*>(error->GetBufferPointer()),error->GetBufferSize());winrt::check_hresult(hr);
    ShaderBinding bindings[]{{0,3,'b',0,0,2},{0,4,'u',0,0,1}};
    StageLayout stage{ShaderStage::Compute,bindings};auto layout=create_pipeline_layout(device.get(),{&stage,1});
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=layout.signature.get();desc.CS={code->GetBufferPointer(),code->GetBufferSize()};
    winrt::com_ptr<ID3D12PipelineState> pipeline;winrt::check_hresult(device->CreateComputePipelineState(&desc,__uuidof(ID3D12PipelineState),pipeline.put_void()));
    winrt::com_ptr<ID3D12CommandAllocator> allocator;winrt::check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.put_void()));
    winrt::com_ptr<ID3D12GraphicsCommandList> cmd;winrt::check_hresult(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.get(),pipeline.get(),__uuidof(ID3D12GraphicsCommandList),cmd.put_void()));
    // A compute stage with no descriptors must still keep its stage identity.
    StageLayout empty{ShaderStage::Compute,{}};auto emptyLayout=create_pipeline_layout(device.get(),{&empty,1});
    DescriptorSnapshot emptySnapshot(device.get(),emptyLayout);emptySnapshot.bind_compute(cmd.get());
    std::vector<DescriptorSnapshot> snapshots;std::vector<winrt::com_ptr<ID3D12Resource>> readbacks;
    snapshots.reserve(2);
    ResourceKey first{ShaderStage::Compute,0,3,0},second{ShaderStage::Compute,0,3,1},out{ShaderStage::Compute,0,4,0};
    for(uint32_t iteration=0;iteration<2;++iteration){
        snapshots.emplace_back(device.get(),layout);auto& s=snapshots.back();
        std::array<uint32_t,4> a{1,2,3,4},b{10,20,30,40};if(iteration)for(int i=0;i<4;++i){a[i]*=10;b[i]*=10;}
        auto bytes=[](const auto& values){return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(values.data()),sizeof(values));};
        rejects([&]{s.bind_compute(cmd.get());});rejects([&]{s.bind_graphics(cmd.get());});
        rejects([&]{s.constants({ShaderStage::Vertex,0,3,0},bytes(a));});
        rejects([&]{s.constants({ShaderStage::Compute,0,3,2},bytes(a));});
        rejects([&]{s.constants(first,{});});
        std::vector<uint8_t> oversized(65537);rejects([&]{s.constants(first,oversized);});
        s.constants(first,bytes(a));s.constants(second,bytes(b));
        rejects([&]{s.constants(first,bytes(a));});rejects([&]{s.bind_compute(cmd.get());});
        a.fill(999);b.fill(999); // Source CPU storage is not retained by a CBV snapshot.
        auto output=buffer(device.get(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto* outputRaw=output.get();
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=1;view.Buffer.StructureByteStride=16;
        s.unordered_access(out,output.get(),view);output=nullptr; // Snapshot owns the remaining resource reference.
        readbacks.push_back(buffer(device.get(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));
        s.bind_compute(cmd.get());rejects([&]{s.constants(first,bytes(a));});cmd->Dispatch(1,1,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={outputRaw,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        cmd->ResourceBarrier(1,&barrier);cmd->CopyResource(readbacks.back().get(),outputRaw);
    }
    winrt::check_hresult(cmd->Close());winrt::com_ptr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
    winrt::check_hresult(device->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.put_void()));ID3D12CommandList* lists[]{cmd.get()};queue->ExecuteCommandLists(1,lists);
    winrt::com_ptr<ID3D12Fence> fence;winrt::check_hresult(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.put_void()));
    winrt::handle event{CreateEventExW(nullptr,nullptr,0,EVENT_ALL_ACCESS)};require(bool(event),"Descriptor fence event unavailable");
    winrt::check_hresult(queue->Signal(fence.get(),1));winrt::check_hresult(fence->SetEventOnCompletion(1,event.get()));require(WaitForSingleObjectEx(event.get(),10000,FALSE)==WAIT_OBJECT_0,"Descriptor fence timeout");
    for(size_t i=0;i<readbacks.size();++i){void* mapped=nullptr;D3D12_RANGE all{0,16};winrt::check_hresult(readbacks[i]->Map(0,&all,&mapped));
        std::array<uint32_t,4> actual{};std::memcpy(actual.data(),mapped,16);D3D12_RANGE noWrite{0,0};readbacks[i]->Unmap(0,&noWrite);
        const uint32_t multiple=i?110:11;for(uint32_t j=0;j<4;++j)require(actual[j]==multiple*(j+1),"Descriptor snapshot array/constants lifetime mismatch");
    }
    std::cout<<"Two immutable descriptor snapshots, CBV arrays/padding, retained UAV and invalid binds passed (WARP)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
