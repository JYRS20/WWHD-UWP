// SPDX-License-Identifier: MPL-2.0
#include "descriptor_snapshot.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace gfxd3d12 {
DescriptorSnapshot::DescriptorSnapshot(ID3D12Device* device,const PipelineLayout& layout):layout_(layout) {
    if(!device || !layout.signature || layout.descriptor_table_count>4)
        throw std::invalid_argument("Invalid descriptor snapshot layout");
    device_.copy_from(device);tables_.resize(layout.descriptor_table_count);
    for(const auto& b:layout.bindings) {
        if(b.root_parameter>=tables_.size() || !b.binding.count ||
           b.descriptor_offset>1000000 || b.binding.count>1000000-b.descriptor_offset)
            throw std::invalid_argument("Invalid descriptor snapshot range");
        if(b.binding.register_type!='b'&&b.binding.register_type!='t'&&b.binding.register_type!='u'&&b.binding.register_type!='s')
            throw std::invalid_argument("Unknown descriptor snapshot type");
        auto& t=tables_[b.root_parameter];const bool sampler=b.binding.register_type=='s';
        if(t.count && t.sampler!=sampler) throw std::invalid_argument("Mixed sampler/resource table");
        t.sampler=sampler;t.count=(std::max)(t.count,b.descriptor_offset+b.binding.count);
    }
    uint32_t resources=0,samplers=0;
    for(auto& t:tables_) {
        if(!t.count) throw std::invalid_argument("Empty descriptor table");
        auto& total=t.sampler?samplers:resources;t.base=total;
        if(t.count>(t.sampler?2048u:1000000u)-total)
            throw std::invalid_argument("Descriptor snapshot exceeds heap capacity");
        total+=t.count;t.filled.resize(t.count,false);
    }
    // Verify dense, non-overlapping table coverage before allocating GPU descriptors.
    for(const auto& b:layout.bindings)for(uint32_t i=0;i<b.binding.count;++i) {
        auto& bits=tables_[b.root_parameter].filled;const auto index=b.descriptor_offset+i;
        if(bits[index]) throw std::invalid_argument("Overlapping descriptor snapshot range");bits[index]=true;
    }
    for(auto& t:tables_) {
        if(std::find(t.filled.begin(),t.filled.end(),false)!=t.filled.end()) throw std::invalid_argument("Holes in descriptor table");
        std::fill(t.filled.begin(),t.filled.end(),false);
    }
    auto heap=[&](D3D12_DESCRIPTOR_HEAP_TYPE type,uint32_t count,winrt::com_ptr<ID3D12DescriptorHeap>& out){
        if(!count)return;D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=type;d.NumDescriptors=count;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        winrt::check_hresult(device_->CreateDescriptorHeap(&d,__uuidof(ID3D12DescriptorHeap),out.put_void()));
    };
    heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,resources,resources_);heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,samplers,samplers_);
}
const DescriptorBinding& DescriptorSnapshot::slot(ResourceKey key,char type) {
    if(sealed_) throw std::logic_error("Descriptor snapshot is already bound and immutable");
    for(const auto& b:layout_.bindings)if(b.stage==key.stage && b.binding.descriptor_set==key.descriptor_set &&
        b.binding.binding==key.binding && b.binding.register_type==type) {
        if(key.array_index>=b.binding.count) throw std::out_of_range("Descriptor array index outside binding");
        if(tables_[b.root_parameter].filled[b.descriptor_offset+key.array_index]) throw std::logic_error("Descriptor slot is already initialized");
        return b;
    }
    throw std::invalid_argument("Shader resource key/type is absent from pipeline layout");
}
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorSnapshot::cpu(const DescriptorBinding& b,uint32_t index) {
    const auto& t=tables_[b.root_parameter];const auto type=t.sampler?D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    auto h=(t.sampler?samplers_:resources_)->GetCPUDescriptorHandleForHeapStart();
    h.ptr+=SIZE_T(t.base+b.descriptor_offset+index)*device_->GetDescriptorHandleIncrementSize(type);return h;
}
void DescriptorSnapshot::filled(const DescriptorBinding& b,uint32_t index){tables_[b.root_parameter].filled[b.descriptor_offset+index]=true;}
void DescriptorSnapshot::retain(ID3D12Resource* r){if(r){winrt::com_ptr<ID3D12Resource> p;p.copy_from(r);retained_.push_back(std::move(p));}}
void DescriptorSnapshot::constants(ResourceKey key,std::span<const uint8_t> data) {
    const auto& b=slot(key,'b');if(data.empty()||data.size()>65536)throw std::invalid_argument("Constant buffer must contain 1..65536 bytes");
    const UINT bytes=UINT((data.size()+255)&~size_t(255));
    D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;winrt::com_ptr<ID3D12Resource> r;
    winrt::check_hresult(device_->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),r.put_void()));
    void* mapped=nullptr;D3D12_RANGE empty{0,0};winrt::check_hresult(r->Map(0,&empty,&mapped));
    std::memset(mapped,0,bytes);std::memcpy(mapped,data.data(),data.size());D3D12_RANGE written{0,bytes};r->Unmap(0,&written);
    D3D12_CONSTANT_BUFFER_VIEW_DESC view{r->GetGPUVirtualAddress(),bytes};device_->CreateConstantBufferView(&view,cpu(b,key.array_index));
    retained_.push_back(std::move(r));filled(b,key.array_index);
}
void DescriptorSnapshot::shader_resource(ResourceKey key,ID3D12Resource* r,const D3D12_SHADER_RESOURCE_VIEW_DESC& view) {
    const auto& b=slot(key,'t');if(!r)throw std::invalid_argument("Missing shader resource");
    retain(r);device_->CreateShaderResourceView(r,&view,cpu(b,key.array_index));filled(b,key.array_index);
}
void DescriptorSnapshot::unordered_access(ResourceKey key,ID3D12Resource* r,const D3D12_UNORDERED_ACCESS_VIEW_DESC& view,ID3D12Resource* counter) {
    const auto& b=slot(key,'u');if(!r)throw std::invalid_argument("Missing unordered access resource");
    retain(r);retain(counter);device_->CreateUnorderedAccessView(r,counter,&view,cpu(b,key.array_index));filled(b,key.array_index);
}
void DescriptorSnapshot::sampler(ResourceKey key,const D3D12_SAMPLER_DESC& view) {
    const auto& b=slot(key,'s');device_->CreateSampler(&view,cpu(b,key.array_index));filled(b,key.array_index);
}
void DescriptorSnapshot::bind(ID3D12GraphicsCommandList* cmd,bool compute) {
    if(!cmd || compute!=layout_.compute)throw std::invalid_argument("Wrong command/stage for descriptor snapshot");
    for(const auto& t:tables_)if(std::find(t.filled.begin(),t.filled.end(),false)!=t.filled.end())
        throw std::logic_error("Descriptor snapshot has uninitialized resources");
    ID3D12DescriptorHeap* heaps[2]{};UINT count=0;if(resources_)heaps[count++]=resources_.get();if(samplers_)heaps[count++]=samplers_.get();
    if(count)cmd->SetDescriptorHeaps(count,heaps);
    if(compute)cmd->SetComputeRootSignature(layout_.signature.get());else cmd->SetGraphicsRootSignature(layout_.signature.get());
    for(UINT i=0;i<tables_.size();++i){const auto& t=tables_[i];
        auto h=(t.sampler?samplers_:resources_)->GetGPUDescriptorHandleForHeapStart();
        h.ptr+=UINT64(t.base)*device_->GetDescriptorHandleIncrementSize(t.sampler?D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if(compute)cmd->SetComputeRootDescriptorTable(i,h);else cmd->SetGraphicsRootDescriptorTable(i,h);
    }
    sealed_=true;
}
void DescriptorSnapshot::bind_graphics(ID3D12GraphicsCommandList* c){bind(c,false);}
void DescriptorSnapshot::bind_compute(ID3D12GraphicsCommandList* c){bind(c,true);}
}
