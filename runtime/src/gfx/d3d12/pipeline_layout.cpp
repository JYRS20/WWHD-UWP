// SPDX-License-Identifier: MPL-2.0
#include "pipeline_layout.h"
#include <limits>
#include <stdexcept>
namespace gfxd3d12 {
PipelineLayout create_pipeline_layout(ID3D12Device* device,std::span<const StageLayout> shaders) {
    if(!device || shaders.empty())throw std::invalid_argument("pipeline layout needs a device and shader stages");
    PipelineLayout result;
    struct Group { ShaderStage stage; bool sampler; uint32_t size=0; std::vector<D3D12_DESCRIPTOR_RANGE> ranges; };
    std::vector<Group> groups;
    bool compute=false,graphics=false;
    unsigned seen=0;
    for(auto const& shader:shaders) {
        unsigned bit;
        switch(shader.stage) {case ShaderStage::Vertex:bit=1;break;case ShaderStage::Fragment:bit=2;break;case ShaderStage::Compute:bit=4;break;default:throw std::invalid_argument("unsupported layout stage");}
        if(seen&bit)throw std::invalid_argument("duplicate pipeline layout stage");seen|=bit;
        compute|=shader.stage==ShaderStage::Compute;graphics|=shader.stage!=ShaderStage::Compute;
        for(auto const& binding:shader.bindings) {
            if(!binding.count || binding.count>65536 || binding.register_index>65536-binding.count)
                throw std::invalid_argument("invalid descriptor register extent");
            D3D12_DESCRIPTOR_RANGE range{};
            switch(binding.register_type) {case 'b':range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_CBV;break;case 't':range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;break;case 's':range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;break;case 'u':range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;break;default:throw std::invalid_argument("unknown descriptor register type");}
            uint32_t group=0;
            for(;group<groups.size();++group)if(groups[group].stage==shader.stage && groups[group].sampler==(binding.register_type=='s'))break;
            if(group==groups.size())groups.push_back({shader.stage,binding.register_type=='s'});
            auto& table=groups[group];
            if(table.size>(std::numeric_limits<uint32_t>::max)()-binding.count)throw std::invalid_argument("descriptor table size overflow");
            result.bindings.push_back({shader.stage,binding,group,table.size});
            range.NumDescriptors=binding.count;range.BaseShaderRegister=binding.register_index;
            range.RegisterSpace=binding.register_space;range.OffsetInDescriptorsFromTableStart=table.size;
            table.ranges.push_back(range);table.size+=binding.count;
        }
    }
    if(compute&&graphics)throw std::invalid_argument("compute and graphics need separate root signatures");
    // CBV/SRV/UAV ranges share one table per stage, samplers another: large resource sets do not
    // spend one root DWORD per resource. No more than four tables for a VS/PS pipeline.
    result.compute=compute;
    result.descriptor_table_count=uint32_t(groups.size());
    std::vector<D3D12_ROOT_PARAMETER> params(groups.size());
    for(size_t i=0;i<groups.size();++i) {
        auto& param=params[i];auto const& table=groups[i];
        param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable={UINT(table.ranges.size()),table.ranges.data()};
        param.ShaderVisibility=table.stage==ShaderStage::Vertex?D3D12_SHADER_VISIBILITY_VERTEX:
            table.stage==ShaderStage::Fragment?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=UINT(params.size());desc.pParameters=params.data();
    desc.Flags=compute?D3D12_ROOT_SIGNATURE_FLAG_NONE:D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    winrt::com_ptr<ID3DBlob> serialized,error;
    HRESULT status=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,serialized.put(),error.put());
    if(FAILED(status))throw std::runtime_error(error?std::string(static_cast<char*>(error->GetBufferPointer()),error->GetBufferSize()):"root-signature serialization failed");
    winrt::check_hresult(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),__uuidof(ID3D12RootSignature),result.signature.put_void()));
    return result;
}
}
