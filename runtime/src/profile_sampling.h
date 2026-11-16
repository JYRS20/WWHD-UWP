// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>

namespace rprof {
// One sample in each 64-operation block, at a new pseudo-random position.
// Fixed-stride timing aliases periodic command batches (notably 512 draws).
// Local PRNG state never touches the game's random generator or timing.
class OperationSampler {
public:
    static constexpr uint32_t kPeriod = 64;
    explicit OperationSampler(uint32_t seed = 0x9e3779b9u) : state_(seed ? seed : 1) {}
    bool take() {
        if (!position_) {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 17;
            state_ ^= state_ << 5;
            selected_ = state_ & (kPeriod - 1);
        }
        const bool selected = position_ == selected_;
        position_ = (position_ + 1) & (kPeriod - 1);
        return selected;
    }
private:
    uint32_t state_, position_ = 0, selected_ = 0;
};
} // namespace rprof
