// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstring>

namespace gx2 {
enum class UniformRegisterBatch { outside, unchanged, changed };

// Only a wholly contained ALU-constant write has homogeneous dirty semantics.
// Crossing either boundary must retain the ordinary per-word classification.
template<uint32_t Base, uint32_t Count>
inline UniformRegisterBatch uniform_register_batch(const uint32_t* regs,
    uint32_t first, const uint32_t* values, uint32_t count) {
    if (first < Base || first - Base >= Count || count > Count - (first - Base))
        return UniformRegisterBatch::outside;
    return std::memcmp(regs + first, values, size_t(count) * sizeof(uint32_t))
        ? UniformRegisterBatch::changed : UniformRegisterBatch::unchanged;
}
}
