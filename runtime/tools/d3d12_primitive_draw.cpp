#include "d3d12_primitive_draw.h"
#include "gfx/d3d12/draw_commands.h"
#include "gfx/d3d12/output_merger_commands.h"
#include "gfx/d3d12/rasterizer_commands.h"
#include "gfx/d3d12/pipeline_layout.h"
#include "gfx/d3d12/descriptor_snapshot.h"
#include <dxgi1_4.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
using namespace gfxd3d12;
namespace {
void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
winrt::com_ptr<ID3D12Resource> buffer(ID3D12Device* d,uint64_t bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state){
 D3D12_HEAP_PROPERTIES properties{};properties.Type=heap;
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 winrt::com_ptr<ID3D12Resource> result;winrt::check_hresult(d->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,__uuidof(ID3D12Resource),result.put_void()));return result;
}
void fill(ID3D12Resource* b,const void* data,size_t bytes){void* p=nullptr;D3D12_RANGE empty{0,0};winrt::check_hresult(b->Map(0,&empty,&p));memcpy(p,data,bytes);D3D12_RANGE written{0,bytes};b->Unmap(0,&written);}
void transition(ID3D12GraphicsCommandList* cmd,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};cmd->ResourceBarrier(1,&b);}
winrt::com_ptr<ID3D12DescriptorHeap> heap(ID3D12Device* d,D3D12_DESCRIPTOR_HEAP_TYPE type,UINT count,bool visible){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=type;desc.NumDescriptors=count;desc.Flags=visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE;winrt::com_ptr<ID3D12DescriptorHeap> result;winrt::check_hresult(d->CreateDescriptorHeap(&desc,__uuidof(ID3D12DescriptorHeap),result.put_void()));return result;}
}
void draw_primitive_shader_test(const HlslShader& vs,std::span<const uint8_t> vsCode,const HlslShader& ps,std::span<const uint8_t> psCode,const std::filesystem::path& output,std::span<const uint32_t> registers,bool indexed,bool clipped){
 constexpr UINT width=128,height=128;
 const auto viewportState=decode_viewport_state(registers,width,height);
 winrt::com_ptr<IDXGIFactory4> factory;winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory4),factory.put_void()));
 winrt::com_ptr<IDXGIAdapter> warp;winrt::check_hresult(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),warp.put_void()));
 winrt::com_ptr<ID3D12Device> device;winrt::check_hresult(D3D12CreateDevice(warp.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),device.put_void()));
 const uint8_t guestIndices[]{0,1,0,2,0,3};
 auto geometry=prepare_draw_geometry(4,3,indexed?4:0,indexed?std::span<const uint8_t>(guestIndices):std::span<const uint8_t>{},indexed?UINT32_MAX:0,indexed?2:1,indexed);
 StageLayout stages[]{{ShaderStage::Vertex,vs.bindings},{ShaderStage::Fragment,ps.bindings}};
 auto layout=create_pipeline_layout(device.get(),stages);
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=layout.signature.get();desc.VS={vsCode.data(),vsCode.size()};desc.PS={psCode.data(),psCode.size()};
 D3D12_INPUT_ELEMENT_DESC attributes[3]{};
 for(UINT i=0;i<3;++i)attributes[i]={"TEXCOORD",i,DXGI_FORMAT_R32G32B32A32_UINT,i,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
 desc.InputLayout={attributes,3};
 auto rasterState=decode_rasterizer_state(registers);
 require(apply_rasterizer_state(desc,rasterState),"real-shader fixture unexpectedly culled entirely");
 auto outputState=decode_output_merger_state(registers,1,false);
 apply_output_merger_state(desc,outputState);
 desc.PrimitiveTopologyType=pipeline_topology_type(geometry.topology);desc.IBStripCutValue=pipeline_strip_cut(geometry);desc.NumRenderTargets=1;desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.SampleMask=~0u;
 winrt::com_ptr<ID3D12PipelineState> pipeline;winrt::check_hresult(device->CreateGraphicsPipelineState(&desc,__uuidof(ID3D12PipelineState),pipeline.put_void()));
 // Fixture contract: two identity matrices, constant colour and a 128x128 viewport transform.
 // This reconstructs authored test state, NOT a recorded game draw or gameplay scene.
 std::array<float,64> vsData{};for(UINT i=0;i<4;++i){vsData[i*4+i]=1;vsData[16+i*4+i]=1;}
 constexpr float color[]{0.75f,0.25f,0.5f,1};
 for(UINT i=0;i<4;++i){vsData[32+i]=color[i];vsData[36+i]=color[i];}
 vsData[40]=viewportState.window_scale_x;vsData[41]=viewportState.window_scale_y;
 std::array<float,64> psData{};psData[0]=1; // Exercise the real pixel shader texture-modulation branch.

 std::array<winrt::com_ptr<ID3D12Resource>,3> vertices;
 D3D12_VERTEX_BUFFER_VIEW views[3]{};
 constexpr float positions[3][4]{{64,16,0,1},{16,112,0,1},{112,112,0,1}};
 for(UINT input=0;input<3;++input){std::array<uint32_t,12> raw{};for(UINT v=0;v<3;++v)for(UINT c=0;c<4;++c){float f=input==2?positions[v][c]:(input==0?1.0f:0.0f);raw[v*4+c]=_byteswap_ulong(std::bit_cast<uint32_t>(f));}vertices[input]=buffer(device.get(),sizeof raw,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);fill(vertices[input].get(),raw.data(),sizeof raw);views[input]={vertices[input]->GetGPUVirtualAddress(),UINT(sizeof raw),16};}
 D3D12_HEAP_PROPERTIES defaults{};defaults.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC textureDesc{};textureDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE1D;textureDesc.Width=1;textureDesc.Height=1;textureDesc.DepthOrArraySize=1;textureDesc.MipLevels=1;textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDesc.SampleDesc.Count=1;
 winrt::com_ptr<ID3D12Resource> texture;winrt::check_hresult(device->CreateCommittedResource(&defaults,D3D12_HEAP_FLAG_NONE,&textureDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),texture.put_void()));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint{};UINT64 textureBytes=0;device->GetCopyableFootprints(&textureDesc,0,1,0,&textureFootprint,nullptr,nullptr,&textureBytes);
 auto textureUpload=buffer(device.get(),textureBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);const uint8_t texel[4]{128,255,64,255};fill(textureUpload.get(),texel,4);
 D3D12_RESOURCE_DESC targetDesc=textureDesc;targetDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;targetDesc.Width=width;targetDesc.Height=height;targetDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
 D3D12_CLEAR_VALUE clear{};clear.Format=targetDesc.Format;clear.Color[3]=1;
 winrt::com_ptr<ID3D12Resource> target;winrt::check_hresult(device->CreateCommittedResource(&defaults,D3D12_HEAP_FLAG_NONE,&targetDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,__uuidof(ID3D12Resource),target.put_void()));
 auto rtv=heap(device.get(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);auto targetView=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.get(),nullptr,targetView);
 DescriptorSnapshot descriptors(device.get(),layout);
 for(const auto& b:layout.bindings) {
  require(b.binding.count==1,"primitive fixture requires scalar resources");
  ResourceKey key{b.stage,b.binding.descriptor_set,b.binding.binding,0};
  if(b.binding.register_type=='b') {
   auto& data=b.stage==ShaderStage::Vertex?vsData:psData;
   descriptors.constants(key,{reinterpret_cast<const uint8_t*>(data.data()),sizeof(data)});
  } else if(b.binding.register_type=='t') {
   D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=textureDesc.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE1D;
   view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture1D.MipLevels=1;
   descriptors.shader_resource(key,texture.get(),view);
  } else if(b.binding.register_type=='s') {
   D3D12_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
   sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
   sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
   descriptors.sampler(key,sampler);
  } else throw std::runtime_error("unexpected primitive shader resource");
 }
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 readbackBytes=0;device->GetCopyableFootprints(&targetDesc,0,1,0,&footprint,nullptr,nullptr,&readbackBytes);
 auto readback=buffer(device.get(),readbackBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 winrt::com_ptr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};winrt::check_hresult(device->CreateCommandQueue(&queueDesc,__uuidof(ID3D12CommandQueue),queue.put_void()));
 winrt::com_ptr<ID3D12CommandAllocator> allocator;winrt::check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),allocator.put_void()));
 winrt::com_ptr<ID3D12GraphicsCommandList> cmd;winrt::check_hresult(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.get(),pipeline.get(),__uuidof(ID3D12GraphicsCommandList),cmd.put_void()));
 D3D12_TEXTURE_COPY_LOCATION uploadSrc{};uploadSrc.pResource=textureUpload.get();uploadSrc.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;uploadSrc.PlacedFootprint=textureFootprint;
 D3D12_TEXTURE_COPY_LOCATION uploadDst{};uploadDst.pResource=texture.get();uploadDst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 cmd->CopyTextureRegion(&uploadDst,0,0,0,&uploadSrc,nullptr);transition(cmd.get(),texture.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
 require(record_viewport_state(cmd.get(),viewportState),"fixture viewport unexpectedly skips draw");
 record_output_merger_constants(cmd.get(),outputState);
 cmd->OMSetRenderTargets(1,&targetView,FALSE,nullptr);cmd->ClearRenderTargetView(targetView,clear.Color,0,nullptr);cmd->SetGraphicsRootSignature(layout.signature.get());
 descriptors.bind_graphics(cmd.get());
 cmd->IASetVertexBuffers(0,3,views);auto indexUpload=record_draw_geometry(device.get(),cmd.get(),geometry);
 transition(cmd.get(),target.get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_TEXTURE_COPY_LOCATION copySrc{};copySrc.pResource=target.get();copySrc.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 D3D12_TEXTURE_COPY_LOCATION copyDst{};copyDst.pResource=readback.get();copyDst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;copyDst.PlacedFootprint=footprint;cmd->CopyTextureRegion(&copyDst,0,0,0,&copySrc,nullptr);
 winrt::check_hresult(cmd->Close());ID3D12CommandList* lists[]{cmd.get()};queue->ExecuteCommandLists(1,lists);
 winrt::com_ptr<ID3D12Fence> fence;winrt::check_hresult(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.put_void()));
 winrt::handle event{CreateEventExW(nullptr,nullptr,0,EVENT_ALL_ACCESS)};require(bool(event),"fence event creation failed");winrt::check_hresult(queue->Signal(fence.get(),1));winrt::check_hresult(fence->SetEventOnCompletion(1,event.get()));require(WaitForSingleObjectEx(event.get(),10000,FALSE)==WAIT_OBJECT_0,"draw fence timeout");
 void* mapped=nullptr;D3D12_RANGE all{0,size_t(readbackBytes)};winrt::check_hresult(readback->Map(0,&all,&mapped));
 std::vector<uint8_t> pixels(width*height*4);for(UINT y=0;y<height;++y)memcpy(pixels.data()+y*width*4,static_cast<uint8_t*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,width*4);D3D12_RANGE noWrite{0,0};readback->Unmap(0,&noWrite);
 auto match=[&](UINT x,UINT y){auto p=pixels.data()+(y*width+x)*4;return abs(int(p[0])-96)<=1 && abs(int(p[1])-64)<=1 && abs(int(p[2])-32)<=1 && p[3]==255;};
 UINT colored=0;for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)colored+=match(x,y);
 const std::string filename=clipped?(indexed?"triangle-clipped-indexed.rgba":"triangle-clipped.rgba"):(indexed?"triangle-indexed.rgba":"triangle.rgba");
 std::ofstream raw(output/filename,std::ios::binary);raw.write(reinterpret_cast<char*>(pixels.data()),pixels.size());raw.close();
 require(match(width/2,height/2) && colored==(clipped?2304u:4608u),"real shader draw produced unexpected pixels");require(pixels[0]==0 && pixels[1]==0 && pixels[2]==0 && pixels[3]==255,"background readback differs");
 if(clipped)for(UINT y=0;y<height;++y)for(UINT x=0;x<width/2;++x){auto p=pixels.data()+(y*width+x)*4;require(p[0]==0&&p[1]==0&&p[2]==0&&p[3]==255,"pixel escaped GX2 scissor");}
 std::cout<<"D3D12 WARP real-shader textured triangle draw/readback PASS, coloured pixels="<<colored<<", indexed="<<indexed<<", clipped="<<clipped<<", instances="<<geometry.instances<<" (authored fixture, not gameplay)\n";
}
