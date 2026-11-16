// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
namespace uwp_texture_watch {
bool requested();
void init(uint8_t* base,uint64_t size);
bool active();
bool verify();
// Atomically retrieve+reset kernel dirty bits, publishing persistent per-page
// epochs so overlapping surfaces cannot consume one another's write history.
// Errors always require full hashing. Call before reading guest bytes.
bool changed(const std::pair<uint32_t,uint32_t>* ranges,size_t count,uint64_t& stamp);
}
