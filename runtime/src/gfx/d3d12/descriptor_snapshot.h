// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "pipeline_layout.h"
namespace gfxd3d12 {
struct ResourceKey {ShaderStage stage;uint32_t descriptor_set,binding,array_index{};};
// Per-submission immutable descriptors. Retain the whole snapshot until its GPU fence completes.
// SRV/UAV resources are retained; their upload storage/state transitions remain the caller's job.
class DescriptorSnapshot {
public:
    DescriptorSnapshot(ID3D12Device*,const PipelineLayout&);
    DescriptorSnapshot(const DescriptorSnapshot&)=delete;
    DescriptorSnapshot& operator=(const DescriptorSnapshot&)=delete;
    DescriptorSnapshot(DescriptorSnapshot&&)=default;
    DescriptorSnapshot& operator=(DescriptorSnapshot&&)=default;
    void constants(ResourceKey,std::span<const uint8_t>);
    void shader_resource(ResourceKey,ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC&);
    void unordered_access(ResourceKey,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC&,ID3D12Resource* counter=nullptr);
    void sampler(ResourceKey,const D3D12_SAMPLER_DESC&);
    void bind_graphics(ID3D12GraphicsCommandList*);
    void bind_compute(ID3D12GraphicsCommandList*);
private:
    struct Table {bool sampler{};uint32_t base{},count{};std::vector<bool> filled;};
    winrt::com_ptr<ID3D12Device> device_;
    PipelineLayout layout_;
    winrt::com_ptr<ID3D12DescriptorHeap> resources_,samplers_;
    std::vector<Table> tables_;
    std::vector<winrt::com_ptr<ID3D12Resource>> retained_;
    bool sealed_{};
    const DescriptorBinding& slot(ResourceKey,char);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(const DescriptorBinding&,uint32_t);
    void filled(const DescriptorBinding&,uint32_t);
    void retain(ID3D12Resource*);
    void bind(ID3D12GraphicsCommandList*,bool);
};
}
