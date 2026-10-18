// Test real GX2 .gsh shaders through the Latte -> GLSL -> SPIR-V -> HLSL -> DXBC path.
// Input and generated shader source must remain private to the game owner.
//   shadertest file.gsh [index]
// Loads the file into guest memory, binds each vertex/pixel shader pair into a
// register file exactly as GX2SetVertexShader/GX2SetPixelShader do, and runs
// the vendored Cemu decompiler.
#include <windows.h>
#include "d3d12_primitive_draw.h"
#include "gfx/d3d12/spirv_hlsl.h"
#include "gfx/d3d12/latte_decompiler.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <stdexcept>
#include <iostream>

#include <cstdio>
#include <bit>
#include <fstream>
#include <vector>

#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "gx2/gx2_regs.h"
#include "ppc.h"
#include "util/helpers/StringBuf.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"

std::unique_ptr<Renderer> g_renderer;
static bool unsupported_decompiler_state=false;
void cemu_shim_log(const std::string& msg) { if(msg.find("Unsupported")!=std::string::npos)unsupported_decompiler_state=true; fprintf(stderr,"[decompiler] %s\n",msg.c_str()); }


struct CompiledShader { gfxd3d12::HlslShader hlsl; Microsoft::WRL::ComPtr<ID3DBlob> code; };
static CompiledShader compile_output(LatteDecompilerOutput_t& output,bool vertex,std::filesystem::path path) {
    std::unique_ptr<LatteDecompilerShader> shader(output.shader);
    if(!shader || shader->hasError || unsupported_decompiler_state || !shader->strBuf_shaderSource)throw std::runtime_error("Latte decompiler emitted no source");
    std::string glsl=shader->strBuf_shaderSource->c_str();
    delete shader->strBuf_shaderSource;shader->strBuf_shaderSource=nullptr;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path.string()+".glsl")<<glsl;
    glslang::TShader stage(vertex?EShLangVertex:EShLangFragment);const char* text=glsl.c_str();stage.setStrings(&text,1);
    stage.setEnvInput(glslang::EShSourceGlsl,vertex?EShLangVertex:EShLangFragment,glslang::EShClientVulkan,100);
    stage.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_1);
    stage.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_3);
    auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
    if(!stage.parse(GetDefaultResources(),450,false,messages))throw std::runtime_error(stage.getInfoLog());
    glslang::TProgram program;program.addShader(&stage);
    if(!program.link(messages))throw std::runtime_error(program.getInfoLog());
    std::vector<uint32_t> words;glslang::SpvOptions options;options.disableOptimizer=true;
    glslang::GlslangToSpv(*program.getIntermediate(vertex?EShLangVertex:EShLangFragment),words,&options);
    auto hlsl=gfxd3d12::spirv_to_hlsl(words,vertex?gfxd3d12::ShaderStage::Vertex:gfxd3d12::ShaderStage::Fragment);
    std::ofstream(path.string()+".hlsl")<<hlsl.source;
    Microsoft::WRL::ComPtr<ID3DBlob> code,error;
    auto status=D3DCompile(hlsl.source.data(),hlsl.source.size(),nullptr,nullptr,nullptr,"main",vertex?"vs_5_1":"ps_5_1",
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(status))throw std::runtime_error(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"D3DCompile failed");
    std::ofstream binary(path.string()+".dxbc",std::ios::binary);binary.write((char*)code->GetBufferPointer(),code->GetBufferSize());
    std::cout<<(vertex?"Vertex":"Pixel")<<": real Latte shader compiled, bindings="<<hlsl.bindings.size()<<"\n";
    return {std::move(hlsl),std::move(code)};
}

struct Block {
    uint32 type, addr, size;
};

int run(int argc, char** argv) {
    if (argc != 3 && !(argc==4 && !strcmp(argv[3],"--draw-primitive"))) {
        fprintf(stderr, "usage: d3d12-gsh-test file.gsh PRIVATE_OUTPUT_DIR\n");
        return 1;
    }
    if (!VirtualAlloc(PPC_MEM_BASE,0x100000000ull,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)) return 1;
    gfxd3d12::select_latte_glsl();
    if(!glslang::InitializeProcess())return 1;

    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8> d((std::istreambuf_iterator<char>(f)), {});
    if(d.size()<32)throw std::runtime_error("truncated GSH header");
    auto be = [&](size_t o) { return (uint32)d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]; };

    // copy every block into guest memory at 0x10000000+ (256-byte aligned, as GX2 requires)
    std::vector<Block> blocks;
    uint32 next = 0x10000000;
    size_t off = be(4);
    while (off + 0x20 <= d.size()) {
        uint32 hs = be(off + 4), type = be(off + 16), size = be(off + 20);
        if(hs<0x20 || hs>d.size()-off || size>d.size()-off-hs)throw std::runtime_error("invalid GSH block extent");
        memcpy(PPC_MEM_BASE + next, &d[off + hs], size);
        blocks.push_back({type, next, size});
        next = (next + size + 0xFF) & ~0xFFu;
        off += hs + size;
    }
    // shader headers (types 3/6) are followed by their programs (types 5/7);
    // point the header's shaderPtr at the loaded program
    uint32 vs = 0, ps = 0;
    for (size_t i = 0; i + 1 < blocks.size(); i++) {
        if (blocks[i].type == 3 && blocks[i + 1].type == 5) {
            vs = blocks[i].addr;
            st32(vs + 0xD0, blocks[i + 1].size);
            st32(vs + 0xD4, blocks[i + 1].addr);
        }
        if (blocks[i].type == 6 && blocks[i + 1].type == 7) {
            ps = blocks[i].addr;
            st32(ps + 0xA4, blocks[i + 1].size);
            st32(ps + 0xA8, blocks[i + 1].addr);
        }
    }
    if(!vs || !ps)throw std::runtime_error("GSH is missing a vertex/pixel pair");
    static uint32 regs[gx2::kNumRegs];
    uint32 size;
    // Authored viewport/scissor registers, not a game state capture.
    regs[Latte::REGADDR::PA_CL_VPORT_XSCALE]=std::bit_cast<uint32_t>(64.0f);
    regs[Latte::REGADDR::PA_CL_VPORT_XOFFSET]=std::bit_cast<uint32_t>(64.0f);
    regs[Latte::REGADDR::PA_CL_VPORT_YSCALE]=std::bit_cast<uint32_t>(-64.0f);
    regs[Latte::REGADDR::PA_CL_VPORT_YOFFSET]=std::bit_cast<uint32_t>(64.0f);
    regs[Latte::REGADDR::PA_CL_VPORT_ZSCALE]=std::bit_cast<uint32_t>(0.5f);
    regs[Latte::REGADDR::PA_CL_VPORT_ZOFFSET]=std::bit_cast<uint32_t>(0.5f);
    regs[Latte::REGADDR::CB_COLOR_CONTROL]=0xccu<<16; // COPY, ordinary colour output
    regs[Latte::REGADDR::PA_SC_GENERIC_SCISSOR_TL]=0;
    regs[Latte::REGADDR::PA_SC_GENERIC_SCISSOR_BR]=128u|(128u<<16);

    // The .gsh supplies register headers but no runtime vertex/texture binding snapshot.
    // Use explicit float4 fetch inputs and the zero-initialized texture state for this compile probe.
    gx2::bind_vertex_shader_regs(regs,vs);gx2::bind_pixel_shader_regs(regs,ps);
    regs[mmCB_COLOR0_INFO]=0x1A<<2;regs[mmCB_COLOR0_BASE]=0x100;
    regs[Latte::REGADDR::CB_TARGET_MASK]=0xF;
    LatteShader_UpdatePSInputs(regs);

    CompiledShader vertex,pixel;
    if (vs) {
        gx2::bind_vertex_shader_regs(regs, vs);
        // fetch shader: one float4 attribute per vertex shader input, each in its own buffer
        LatteFetchShader fs;
        // attribInfo pointers inside .gsh files are relocatable offsets (0xD06xxxxx) into the header block
        uint32 nattr = ld32(vs + 0x104), attrs = vs + (ld32(vs + 0x108) & 0xFFFFF);
        static LatteParsedFetchShaderAttribute pa[16];
        for (uint32 i = 0; i < nattr && i < 16; i++) {
            // GX2AttribVar: +0 name, +4 type, +8 count, +C location
            uint32 loc = ld32(attrs + i * 16 + 12);
            LatteParsedFetchShaderAttribute& a = pa[i];
            a = {};
            a.attributeBufferIndex = (uint8)i;
            a.semanticId = (uint8)loc;
            a.format = Latte::E_HWFMT::HWFMT_32_32_32_32_FLOAT;
            a.nfa = 2; // Float input numeric interpretation, matching the authored float4 fetch state.
            a.fetchType = LatteConst::VertexFetchType2::VERTEX_DATA;
            a.endianSwap = LatteConst::VertexFetchEndianMode::SWAP_U32;
            a.ds[0] = 0; a.ds[1] = 1; a.ds[2] = 2; a.ds[3] = 3;
            LatteParsedFetchShaderBufferGroup g{};
            g.attributeBufferIndex = (uint8)i;
            g.attribCount = 1;
            g.attrib = &a;
            fs.bufferGroups.push_back(g);
            fs.attributeBufferMask |= 1u << i;
            regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + i * 7 + 2] = 16 << 11;  // stride
            printf("// attribute %u -> semantic %u\n", i, loc);
        }
        uint32 prog = gx2::vertex_shader_program(vs, &size);
        LatteDecompilerOptions opt; opt.strictMul=true;
        LatteDecompilerOutput_t out{};
        LatteDecompiler_DecompileVertexShader(0x1234, regs, PPC_MEM_BASE + prog, size, &fs, opt, &out);
        vertex=compile_output(out,true,std::filesystem::path(argv[2])/"vertex");
        fs.bufferGroups.clear();
    }
    if (ps) {
        gx2::bind_pixel_shader_regs(regs, ps);
        regs[mmCB_COLOR0_INFO] = 0x1A << 2;  // RGBA8 render target
        regs[mmCB_COLOR0_BASE] = 0x100;
        regs[Latte::REGADDR::CB_TARGET_MASK] = 0xF;
        uint32 prog = gx2::pixel_shader_program(ps, &size);
        LatteDecompilerOptions opt; opt.strictMul=true;
        LatteDecompilerOutput_t out{};
        LatteDecompiler_DecompilePixelShader(0x5678, regs, PPC_MEM_BASE + prog, size, opt, &out);
        pixel=compile_output(out,false,std::filesystem::path(argv[2])/"pixel");
    }
    if(argc==4)for(bool clipped : {false,true})for(bool indexed : {false,true}) {
        regs[Latte::REGADDR::PA_SC_GENERIC_SCISSOR_TL]=clipped?64:0;
        draw_primitive_shader_test(vertex.hlsl,{static_cast<const uint8_t*>(vertex.code->GetBufferPointer()),vertex.code->GetBufferSize()},
            pixel.hlsl,{static_cast<const uint8_t*>(pixel.code->GetBufferPointer()),pixel.code->GetBufferSize()},
            std::filesystem::path(argv[2]),std::span<const uint32_t>(regs),indexed,clipped);
    }
    VirtualFree(PPC_MEM_BASE,0,MEM_RELEASE);
    std::cout<<"Real GSH vertex and pixel shader compilation PASS (no gameplay scene rendered)\n";
    return 0;
}

int main(int argc,char** argv){try{return run(argc,argv);}catch(std::exception const& e){std::cerr<<e.what()<<"\n";return 1;}}
