// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gfxvk {
// CPU-only, per-shader memo of register-only support uniforms. Guest-backed
// remapped inputs and stereo bypass it. Compare every source byte; never use
// an address, sparse hash, frame number or dirty-register hint as equality.
class UniformPackMemo {
public:
    struct Range { uint32_t word, bytes; };
    bool initialized = false, eligible = false;
    bool initialize(std::vector<Range> ranges, size_t outputBytes, size_t& budget) {
        initialized = true;
        if (!outputBytes || outputBytes > 8192 || ranges.size() > 1024) return reject();
        std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.word < b.word; });
        for (auto range : ranges) {
            if (!range.bytes || range.bytes % 4 || range.word >= 0x10000 || range.bytes/4 > 0x10000-range.word) return reject();
            if (!ranges_.empty() && range.word <= ranges_.back().word + ranges_.back().bytes/4) {
                auto& previous = ranges_.back();
                previous.bytes = std::max(previous.bytes, (range.word-previous.word)*4+range.bytes);
            } else ranges_.push_back(range);
        }
        size_t bytes = 0;
        for (auto range : ranges_) bytes += range.bytes;
        if (bytes > 8192) return reject();
        const size_t storage = bytes + outputBytes + ranges_.capacity()*sizeof(Range);
        if (storage > budget) return reject();
        input_.resize(bytes); output_.resize(outputBytes);
        const size_t retained = input_.capacity() + output_.capacity() + ranges_.capacity()*sizeof(Range);
        if (retained > budget) return reject();
        budget -= retained;
        eligible = true;
        return true;
    }
    bool matches(const uint32_t* regs, std::array<uint32_t,2> scales, uint64_t& compared) const {
        if (!filled_ || scales_ != scales) return false;
        size_t offset = 0;
        for (auto range : ranges_) {
            compared += range.bytes;
            if (std::memcmp(regs+range.word, input_.data()+offset, range.bytes)) return false;
            offset += range.bytes;
        }
        return true;
    }
    void store(const uint32_t* regs, std::array<uint32_t,2> scales, const std::vector<uint8_t>& output) {
        size_t offset = 0;
        for (auto range : ranges_) {
            std::memcpy(input_.data()+offset, regs+range.word, range.bytes);
            offset += range.bytes;
        }
        output_ = output;
        scales_ = scales;
        filled_ = true;
    }
    void copy_to(std::vector<uint8_t>& out) const { out = output_; }
private:
    bool reject() {
        eligible = false; filled_ = false;
        std::vector<Range>{}.swap(ranges_);
        std::vector<uint8_t>{}.swap(input_);
        std::vector<uint8_t>{}.swap(output_);
        return false;
    }
    bool filled_ = false;
    std::array<uint32_t,2> scales_{};
    std::vector<Range> ranges_;
    std::vector<uint8_t> input_, output_;
};
} // namespace gfxvk
