#include "gfx/d3d12/texture_upload.h"
#include "gfx/d3d12/bc45_decode.h"
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
using namespace gfxd3d12;
static void require(bool v,const char* message) { if(!v) throw std::runtime_error(message); }
static winrt::com_ptr<ID3D12Resource> buffer(ID3D12Device* d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false) {
    D3D12_HEAP_PROPERTIES h{};h.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(uav) desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    winrt::com_ptr<ID3D12Resource> r;
    winrt::check_hresult(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,__uuidof(ID3D12Resource),r.put_void()));return r;
}
static LinearTexture2D fixture(UINT w,UINT h,UINT channels) {
    LinearTexture2D t{w,h,channels==1?0x34u:0x35u,((w+3)/4)*channels*8,(h+3)/4,{}};
    t.data.resize(size_t(t.row_bytes)*t.block_rows);
    for(UINT by=0;by<t.block_rows;++by) for(UINT bx=0;bx<(w+3)/4;++bx) for(UINT c=0;c<channels;++c) {
        auto* b=t.data.data()+size_t(by)*t.row_bytes+(size_t(bx)*channels+c)*8;
        bool low=(bx+by+c)&1;b[0]=low?0:255;b[1]=low?255:0;
        uint64_t bits=0;for(UINT i=0;i<16;++i) bits|=uint64_t((i+bx*3+by*5+c)%8)<<(3*i);
        for(UINT i=0;i<6;++i) b[2+i]=uint8_t(bits>>(i*8));
    }
    return t;
}
static UINT expected(UINT x,UINT y,UINT c) {
    // Independent golden endpoint tables, covering every index in both BC UNORM modes.
    constexpr UINT high[]{255,0,219,182,146,109,73,36};
    constexpr UINT low[]{0,255,51,102,153,204,0,255};
    UINT bx=x/4,by=y/4,index=((y%4)*4+x%4+bx*3+by*5+c)%8;
    return ((bx+by+c)&1)?low[index]:high[index];
}
static LinearTexture2D private_bflim(const char* path) {
    std::ifstream in(path,std::ios::binary);require(bool(in),"Private BFLIM unavailable");
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),{});
    require(b.size()>=40,"Private BFLIM truncated");const size_t p=b.size()-40;
    auto be=[&](size_t off,size_t count) {uint32_t v=0;for(size_t i=0;i<count;++i) v=(v<<8)|b[p+off+i];return v;};
    require(be(0,4)==0x464c494d && be(4,2)==0xfeff && be(20,4)==0x696d6167,"Private BFLIM footer mismatch");
    require(be(12,4)==b.size() && be(36,4)==p,"Private BFLIM size mismatch");
    uint32_t format=b[p+34]==15||b[p+34]==16?0x34:(b[p+34]==17?0x35:0);
    require(format!=0,"Private fixture accepts BC4/BC5 UNORM only");
    Texture2DLayout layout{be(28,2),be(30,2),format,uint32_t(b[p+35]&31),uint32_t(b[p+35]>>5)<<8,0};
    return untile_texture_2d(layout,std::span<const uint8_t>(b.data(),p));
}
int main(int argc,char** argv) { try {
    winrt::com_ptr<IDXGIFactory4> factory;winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory4),factory.put_void()));
    winrt::com_ptr<IDXGIAdapter> warp;winrt::check_hresult(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),warp.put_void()));
    winrt::com_ptr<ID3D12Device> device;winrt::check_hresult(D3D12CreateDevice(warp.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),device.put_void()));
    const char* shader=R"(
Texture2D<float4> image : register(t0);
SamplerState pointWrap : register(s0);
RWStructuredBuffer<float4> result : register(u0);
cbuffer Dimensions : register(b0) { uint width; uint height; };
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
 if(id.x>=width || id.y>=height) return;
 float2 uv=(float2(id.xy)+float2(width,height)+0.5)/float2(width,height);
 result[id.y*width+id.x]=image.SampleLevel(pointWrap,uv,0);
})";
    winrt::com_ptr<ID3DBlob> bytecode,errors;
    auto hr=D3DCompile(shader,std::strlen(shader),"authored-texture-fixture",nullptr,nullptr,"main","cs_5_1",
        D3DCOMPILE_ENABLE_STRICTNESS,0,bytecode.put(),errors.put());
    if(FAILED(hr) && errors) std::cerr.write(static_cast<char*>(errors->GetBufferPointer()),errors->GetBufferSize());
    winrt::check_hresult(hr);
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={1,&range};
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[1].Descriptor={0,0};
    params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,2};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    D3D12_ROOT_SIGNATURE_DESC root{3,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    winrt::com_ptr<ID3DBlob> serial;winrt::check_hresult(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,serial.put(),nullptr));
    winrt::com_ptr<ID3D12RootSignature> signature;
    winrt::check_hresult(device->CreateRootSignature(0,serial->GetBufferPointer(),serial->GetBufferSize(),__uuidof(ID3D12RootSignature),signature.put_void()));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=signature.get();pso.CS={bytecode->GetBufferPointer(),bytecode->GetBufferSize()};
    winrt::com_ptr<ID3D12PipelineState> pipeline;winrt::check_hresult(device->CreateComputePipelineState(&pso,__uuidof(ID3D12PipelineState),pipeline.put_void()));
    D3D12_COMMAND_QUEUE_DESC q{};winrt::com_ptr<ID3D12CommandQueue> queue;
    winrt::check_hresult(device->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.put_void()));
    UINT cases=0;UINT64 pixels=0;
    std::vector<LinearTexture2D> inputs;
    for(auto dims : {std::array<UINT,2>{1,1},{5,7},{1010,150},{104,60}}) for(UINT channels : {1u,2u})
        inputs.push_back(fixture(dims[0],dims[1],channels));
    const size_t authored=inputs.size();
    for(int i=1;i<argc;++i) inputs.push_back(private_bflim(argv[i]));
    for(size_t caseIndex=0;caseIndex<inputs.size();++caseIndex) {
        const auto& input=inputs[caseIndex];
        auto decoded=decode_bc45_unorm(input);
        const UINT channels=input.format==0x34?1:2;
        const std::array<UINT,2> dims{input.width,input.height};
        for(UINT y=0;y<input.height;++y) for(UINT x=0;x<input.width;++x) for(UINT c=0;c<channels;++c)
            if(caseIndex<authored) require(decoded.data[size_t(y)*decoded.row_bytes+x*channels+c]==expected(x,y,c),"BC CPU golden-table mismatch");
        for(int bad=0;bad<3;++bad) {
            auto invalid=input;if(bad==0) invalid.data.pop_back();if(bad==1) invalid.row_bytes++;if(bad==2) invalid.format=0x234;
            bool rejected=false;try{(void)decode_bc45_unorm(invalid);}catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"Invalid BC fallback input accepted");
        }
        winrt::com_ptr<ID3D12CommandAllocator> allocator;
        winrt::check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.put_void()));
        winrt::com_ptr<ID3D12GraphicsCommandList> commands;
        winrt::check_hresult(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.get(),pipeline.get(),__uuidof(ID3D12GraphicsCommandList),commands.put_void()));
        auto texture=record_texture_upload_2d(device.get(),commands.get(),input);
        auto desc=texture.texture->GetDesc();require(desc.Width==input.width && desc.Height==input.height,"Logical texture dimensions changed");
        bool fallback=(input.width%4)||(input.height%4);
        require(desc.Format==(fallback?(channels==1?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R8G8_UNORM):
            (channels==1?DXGI_FORMAT_BC4_UNORM:DXGI_FORMAT_BC5_UNORM)),"Unexpected upload format");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        winrt::com_ptr<ID3D12DescriptorHeap> descriptors;
        winrt::check_hresult(device->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),descriptors.put_void()));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(texture.texture.get(),&srv,descriptors->GetCPUDescriptorHandleForHeapStart());
        const UINT64 bytes=UINT64(input.width)*input.height*4*sizeof(float);
        auto output=buffer(device.get(),bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        auto readback=buffer(device.get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        commands->SetComputeRootSignature(signature.get());ID3D12DescriptorHeap* heaps[]{descriptors.get()};commands->SetDescriptorHeaps(1,heaps);
        commands->SetComputeRootDescriptorTable(0,descriptors->GetGPUDescriptorHandleForHeapStart());
        commands->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());commands->SetComputeRoot32BitConstants(2,2,dims.data(),0);
        commands->Dispatch((input.width+7)/8,(input.height+7)/8,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={output.get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        commands->ResourceBarrier(1,&barrier);commands->CopyResource(readback.get(),output.get());
        winrt::check_hresult(commands->Close());ID3D12CommandList* lists[]{commands.get()};queue->ExecuteCommandLists(1,lists);
        winrt::com_ptr<ID3D12Fence> fence;winrt::check_hresult(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.put_void()));
        winrt::handle event{CreateEventExW(nullptr,nullptr,0,EVENT_ALL_ACCESS)};require(bool(event),"Fence event unavailable");
        winrt::check_hresult(queue->Signal(fence.get(),1));winrt::check_hresult(fence->SetEventOnCompletion(1,event.get()));
        require(WaitForSingleObjectEx(event.get(),10000,FALSE)==WAIT_OBJECT_0,"Texture sampling fence timeout");
        void* mapped=nullptr;D3D12_RANGE all{0,size_t(bytes)};winrt::check_hresult(readback->Map(0,&all,&mapped));
        const float* values=static_cast<const float*>(mapped);bool matches=true;
        for(UINT y=0;y<input.height;++y) for(UINT x=0;x<input.width;++x) for(UINT c=0;c<4;++c) {
            float want=c<channels?float(decoded.data[size_t(y)*decoded.row_bytes+x*channels+c])/255.0f:(c==3?1.0f:0.0f);
            float actual=values[(size_t(y)*input.width+x)*4+c];
            matches &= std::isfinite(actual) && std::abs(actual-want)<=1.1f/255.0f;
        }
        D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);
        require(matches,"BC native/fallback point-wrap shader sampling mismatch");++cases;pixels+=UINT64(input.width)*input.height;
    }
    std::cout<<cases<<" BC4/BC5 native/fallback shader sampling cases, "<<pixels<<" pixels passed (WARP)\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
