// Standalone desktop compiler test: no game imagery, assets or D3D12 device required.
#include "gfx/d3d12/spirv_hlsl.h"
#include "gfx/d3d12/pipeline_layout.h"
#include <dxgi1_4.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace gfxd3d12;
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static std::vector<uint32_t> compile_glsl(const char* text,EShLanguage stage){
    glslang::TShader shader(stage);shader.setStrings(&text,1);
    shader.setEnvInput(glslang::EShSourceGlsl,stage,glslang::EShClientVulkan,450);
    shader.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_3);
    auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
    if(!shader.parse(GetDefaultResources(),450,false,messages))throw std::runtime_error(shader.getInfoLog());
    glslang::TProgram program;program.addShader(&shader);
    if(!program.link(messages))throw std::runtime_error(program.getInfoLog());
    std::vector<uint32_t> words;glslang::SpvOptions options;options.disableOptimizer=true;
    glslang::GlslangToSpv(*program.getIntermediate(stage),words,&options);return words;
}
struct CheckedShader { HlslShader hlsl; ComPtr<ID3DBlob> code; };
static CheckedShader check_shader(const char* glsl,EShLanguage sourceStage,ShaderStage targetStage,const char* profile){
    auto result=spirv_to_hlsl(compile_glsl(glsl,sourceStage),targetStage);
    ComPtr<ID3DBlob> bytecode,error;
    HRESULT status=D3DCompile(result.source.data(),result.source.size(),nullptr,nullptr,nullptr,"main",profile,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&error);
    if(FAILED(status))throw std::runtime_error(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"D3DCompile failed");
    ComPtr<ID3D12ShaderReflection> reflection;
    require(SUCCEEDED(D3DReflect(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),IID_PPV_ARGS(&reflection))),"reflection failed");
    D3D12_SHADER_DESC desc{};require(SUCCEEDED(reflection->GetDesc(&desc)),"shader description failed");
    require(desc.BoundResources==result.bindings.size(),"reflection/resource mapping count differs");
    for(auto const& expected:result.bindings){
        bool found=false;
        for(UINT i=0;i<desc.BoundResources;++i){
            D3D12_SHADER_INPUT_BIND_DESC actual{};reflection->GetResourceBindingDesc(i,&actual);
            char kind=actual.Type==D3D_SIT_CBUFFER?'b':actual.Type==D3D_SIT_SAMPLER?'s':
                (actual.Type==D3D_SIT_TEXTURE||actual.Type==D3D_SIT_BYTEADDRESS||actual.Type==D3D_SIT_STRUCTURED)?'t':'u';
            found|=kind==expected.register_type && actual.Space==expected.register_space && actual.BindPoint==expected.register_index && actual.BindCount==expected.count;
        }
        require(found,"D3D reflection does not match descriptor metadata");
    }
    std::cout<<profile<<": GLSL -> SPIR-V -> HLSL -> DXBC + reflection PASS\n";
    return {std::move(result),std::move(bytecode)};
}
int main(){try{
    require(glslang::InitializeProcess(),"glslang initialization failed");
    auto vertex=check_shader(R"(#version 450
layout(location=0) in vec3 position;
layout(location=0) out vec2 uv;
layout(set=2,binding=3) uniform Frame { vec4 scale; } frame;
layout(push_constant) uniform Push { vec4 offset; } pushData;
void main(){gl_Position=vec4(position*frame.scale.xyz+pushData.offset.xyz,1.0);uv=position.xy;}
)",EShLangVertex,ShaderStage::Vertex,"vs_5_1");
    // Vulkan bindings 7[2] and 8 are legal; direct HLSL register mapping would overlap.
    auto pixel=check_shader(R"(#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=1,binding=7) uniform sampler2D tex[2];
layout(set=1,binding=8) uniform sampler2D other;
void main(){color=texture(tex[0],uv)+texture(tex[1],uv)+texture(other,uv);}
)",EShLangFragment,ShaderStage::Fragment,"ps_5_1");
    auto compute=check_shader(R"(#version 450
layout(local_size_x=1) in;
layout(set=3,binding=9,std430) buffer Values { vec4 color[]; } values;
layout(set=3,binding=10,rgba32f) uniform image2D outputImage;
void main(){imageStore(outputImage,ivec2(gl_GlobalInvocationID.xy),values.color[0]);}
)",EShLangCompute,ShaderStage::Compute,"cs_5_1");
    // Software D3D12 validation proves PSO/root-signature compatibility, not Xbox rendering.
    ComPtr<IDXGIFactory4> factory;require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory failed");
    ComPtr<IDXGIAdapter> warp;require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))),"WARP unavailable");
    ComPtr<ID3D12Device> device;require(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"WARP D3D12 device failed");
    StageLayout stages[]{{ShaderStage::Vertex,vertex.hlsl.bindings},{ShaderStage::Fragment,pixel.hlsl.bindings}};
    auto layout=create_pipeline_layout(device.Get(),stages);
    require(layout.descriptor_table_count==3,"resource tables should be grouped per stage and heap type");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=layout.signature.get();
    desc.VS={vertex.code->GetBufferPointer(),vertex.code->GetBufferSize()};desc.PS={pixel.code->GetBufferPointer(),pixel.code->GetBufferSize()};
    D3D12_INPUT_ELEMENT_DESC input{"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};desc.InputLayout={&input,1};
    desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=desc.BlendState.RenderTarget[0];blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.NumRenderTargets=1;desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.SampleMask=~0u;
    ComPtr<ID3D12PipelineState> graphics;require(SUCCEEDED(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&graphics))),"graphics pipeline creation failed");
    StageLayout cs{ShaderStage::Compute,compute.hlsl.bindings};auto csLayout=create_pipeline_layout(device.Get(),std::span(&cs,1));
    require(csLayout.descriptor_table_count==1 && csLayout.bindings.size()==2 && csLayout.bindings[1].descriptor_offset==1,"compute resources should share a contiguous table");
    D3D12_COMPUTE_PIPELINE_STATE_DESC csDesc{};csDesc.pRootSignature=csLayout.signature.get();csDesc.CS={compute.code->GetBufferPointer(),compute.code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> csPipeline;require(SUCCEEDED(device->CreateComputePipelineState(&csDesc,IID_PPV_ARGS(&csPipeline))),"compute pipeline creation failed");
    std::cout<<"D3D12 WARP graphics/compute root signatures and PSOs PASS (no draw submitted)\n";
    bool mixed=false;StageLayout bad[]{{ShaderStage::Compute,compute.hlsl.bindings},{ShaderStage::Vertex,vertex.hlsl.bindings}};
    try{create_pipeline_layout(device.Get(),bad);}catch(std::exception const&){mixed=true;}
    require(mixed,"mixed graphics/compute layout accepted");
    bool invalid=false;try{spirv_to_hlsl({},ShaderStage::Vertex);}catch(std::exception const&){invalid=true;}
    require(invalid,"invalid SPIR-V was accepted");
    auto words=compile_glsl("#version 450\nlayout(location=0) out vec4 c;void main(){c=vec4(1.0);}",EShLangFragment);
    bool mismatch=false;try{spirv_to_hlsl(words,ShaderStage::Vertex);}catch(std::exception const&){mismatch=true;}
    require(mismatch,"wrong shader stage was accepted");
    auto reserved=compile_glsl("#version 450\nlayout(set=32,binding=0) uniform sampler2D t;layout(location=0) out vec4 c;void main(){c=texture(t,vec2(0));}",EShLangFragment);
    bool collision=false;try{spirv_to_hlsl(reserved,ShaderStage::Fragment);}catch(std::exception const&){collision=true;}
    require(collision,"reserved push-constant space was accepted as a descriptor set");
    glslang::FinalizeProcess();std::cout<<"Invalid input, wrong stage and reserved descriptor-space rejection PASS\n";return 0;
}catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}}
