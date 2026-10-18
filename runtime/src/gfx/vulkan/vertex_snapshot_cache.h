// Bounded immutable vertex snapshots. Never trusts guest addresses without comparing fresh bytes.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gfxvk {
struct VertexSnapshotCacheStats {
    uint64_t requests=0, comparisons=0, hits=0, reusedBytes=0, copiedBytes=0, evictions=0;
    uint64_t shadowBytes=0;
};
VertexSnapshotCacheStats vertex_snapshot_associative_stats();

template<class Slice, size_t Sets=32, size_t Ways=4, size_t MaxPayload=256*1024>
class VertexSnapshotCache {
    struct Entry {
        uint32_t address=0,size=0;
        uint64_t age=0;
        bool valid=false,direct=false;
        Slice slice{};
        std::vector<uint8_t> bytes;
    };
    std::array<std::array<Entry,Ways>,Sets> entries{};
    uintptr_t device=0;
    uint64_t generation=~uint64_t(0),clock=0;
    VertexSnapshotCacheStats counts{};
    static size_t bucket(uint32_t address,uint32_t size) {
        uint64_t key=(uint64_t(address)<<32)|size;
        key^=key>>30;key*=0xbf58476d1ce4e5b9ull;key^=key>>27;
        return size_t(key%Sets);
    }
public:
    static bool eligible(uint32_t size) { return size>0 && size<=MaxPayload; }
    void begin(uintptr_t currentDevice,uint64_t currentGeneration) {
        if(device==currentDevice && generation==currentGeneration)return;
        for(auto& set:entries)for(auto& e:set) { e.valid=false;e.slice={};e.age=0; }
        device=currentDevice;generation=currentGeneration;clock=0;
    }
    VertexSnapshotCacheStats stats() const { return counts; }
    template<class Make>
    Slice get(uint32_t address,uint32_t size,const void* fresh,bool direct,Make&& make) {
        // Ineligible payloads retain the caller's original snapshot path.
        if(!eligible(size))return make(fresh,size);
        ++counts.requests;
        auto& set=entries[bucket(address,size)];
        Entry* chosen=nullptr;
        for(auto& e:set) {
            if(!e.valid || e.address!=address || e.size!=size)continue;
            ++counts.comparisons;
            if(!std::memcmp(fresh,e.direct?e.slice.mapped:e.bytes.data(),size)) {
                ++counts.hits;counts.reusedBytes+=size;e.age=++clock;return e.slice;
            }
            chosen=&e;break; // Changed bytes replace this key; never retain an older version.
        }
        if(!chosen) {
            for(auto& e:set)if(!e.valid) {chosen=&e;break;}
            if(!chosen) {
                chosen=&set[0];for(auto& e:set)if(e.age<chosen->age)chosen=&e;
                ++counts.evictions;
            }
        }
        chosen->valid=false; // A failed allocation cannot publish partially updated metadata.
        const void* source=fresh;
        if(!direct) {
            if(chosen->bytes.capacity()<size) {
                const auto old=chosen->bytes.capacity();
                std::vector<uint8_t>(size).swap(chosen->bytes);
                counts.shadowBytes+=chosen->bytes.capacity()-old;
            } else chosen->bytes.resize(size);
            std::memcpy(chosen->bytes.data(),fresh,size);source=chosen->bytes.data();
        }
        // The CPU shadow and the GPU slice originate from exactly the same copy.
        auto slice=make(source,size);
        chosen->slice=slice;chosen->address=address;chosen->size=size;
        chosen->direct=direct;chosen->age=++clock;chosen->valid=true;
        counts.copiedBytes+=size;
        return slice;
    }
};
}
