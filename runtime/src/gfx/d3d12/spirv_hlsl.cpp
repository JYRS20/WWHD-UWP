// SPDX-License-Identifier: MPL-2.0
#include "spirv_hlsl.h"
#include <spirv_hlsl.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <stdexcept>
namespace gfxd3d12 {
HlslShader spirv_to_hlsl(std::span<const uint32_t> words, ShaderStage stage) {
    if(words.size()<5 || words[0]!=0x07230203u) throw std::invalid_argument("invalid SPIR-V header");
    spv::ExecutionModel model;
    switch(stage) {
    case ShaderStage::Vertex: model=spv::ExecutionModelVertex;break;
    case ShaderStage::Fragment: model=spv::ExecutionModelFragment;break;
    case ShaderStage::Compute: model=spv::ExecutionModelGLCompute;break;
    default: throw std::invalid_argument("unsupported D3D12 shader stage");
    }
    spirv_cross::CompilerHLSL compiler(words.data(),words.size());
    compiler.set_entry_point("main",model);
    auto options=compiler.get_hlsl_options();
    options.shader_model=51;
    options.force_storage_buffer_as_uav=true;
    // Unsupported point-sprite builtins stay errors; never substitute fake point coordinates.
    compiler.set_hlsl_options(options);
    auto resources=compiler.get_shader_resources();
    if(!resources.subpass_inputs.empty() || !resources.atomic_counters.empty())
        throw std::invalid_argument("subpass inputs and atomic counters need explicit D3D12 adaptation");
    struct Item { spirv_cross::Resource resource; std::string types; };
    std::vector<Item> items;
    auto add=[&](auto const& list,const char* types){for(auto const& res:list)items.push_back({res,types});};
    add(resources.uniform_buffers,"b"); add(resources.storage_buffers,"u");
    add(resources.sampled_images,"ts"); add(resources.separate_images,"t");
    add(resources.separate_samplers,"s"); add(resources.storage_images,"u");
    std::sort(items.begin(),items.end(),[&](auto const& a,auto const& b){
        auto key=[&](auto const& x){return std::pair{compiler.get_decoration(x.resource.id,spv::DecorationDescriptorSet),compiler.get_decoration(x.resource.id,spv::DecorationBinding)};};
        return key(a)<key(b);
    });
    HlslShader result;
    std::map<std::pair<uint32_t,char>,uint32_t> next;
    for(auto const& item:items) {
        auto const& res=item.resource;
        if(!compiler.has_decoration(res.id,spv::DecorationDescriptorSet) || !compiler.has_decoration(res.id,spv::DecorationBinding))
            throw std::invalid_argument("a shader resource has no explicit descriptor set/binding");
        uint32_t set=compiler.get_decoration(res.id,spv::DecorationDescriptorSet);
        uint32_t binding=compiler.get_decoration(res.id,spv::DecorationBinding);
        if(set>=32) throw std::invalid_argument("descriptor set collides with reserved D3D12 auxiliary space");
        auto const& type=compiler.get_type(res.type_id);
        uint32_t count=1;
        for(size_t i=0;i<type.array.size();++i) {
            if(!type.array_size_literal[i] || !type.array[i] || type.array[i]>65536/count)
                throw std::invalid_argument("unsupported descriptor array extent");
            count*=type.array[i];
        }
        spirv_cross::HLSLResourceBinding remap{};
        remap.stage=model; remap.desc_set=set; remap.binding=binding;
        for(char kind:item.types) {
            auto& cursor=next[{set,kind}];
            if(cursor>65536-count) throw std::invalid_argument("descriptor register range overflow");
            spirv_cross::HLSLResourceBinding::Binding target{set,cursor};
            switch(kind) {case 'b':remap.cbv=target;break;case 't':remap.srv=target;break;case 's':remap.sampler=target;break;case 'u':remap.uav=target;break;}
            result.bindings.push_back({set,binding,kind,set,cursor,count}); cursor+=count;
        }
        compiler.add_hlsl_resource_binding(remap);
    }
    if(resources.push_constant_buffers.size()>1) throw std::invalid_argument("multiple push-constant buffers");
    if(!resources.push_constant_buffers.empty()) {
        auto size=compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers[0].base_type_id));
        if(size>65536) throw std::invalid_argument("push constants exceed constant-buffer capacity");
        result.push_constant_bytes=uint32_t(size);
        spirv_cross::HLSLResourceBinding remap{};
        remap.stage=model; remap.desc_set=spirv_cross::ResourceBindingPushConstantDescriptorSet;
        remap.binding=spirv_cross::ResourceBindingPushConstantBinding; remap.cbv={32,0};
        compiler.add_hlsl_resource_binding(remap);
        result.bindings.push_back({remap.desc_set,remap.binding,'b',32,0,1});
    }
    result.source=compiler.compile();
    if(result.source.empty()) throw std::runtime_error("empty HLSL output");
    return result;
}
}
