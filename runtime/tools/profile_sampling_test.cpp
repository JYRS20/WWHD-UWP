// SPDX-License-Identifier: MPL-2.0
#include "../src/profile_sampling.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}
int main() {
    for (uint32_t seed : {0u, 1u, 0x9e3779b9u, 0x85ebca6bu}) {
        rprof::OperationSampler sampler(seed), repeat(seed);
        std::array<unsigned,64> histogram{};
        unsigned boundaries = 0;
        constexpr unsigned blocks = 65536;
        for (unsigned b = 0; b < blocks; ++b) {
            unsigned count = 0;
            for (unsigned p = 0; p < 64; ++p) {
                const bool selected = sampler.take();
                check(selected == repeat.take(), "same seed must reproduce the measurement stream");
                if (selected) { ++count; ++histogram[p]; boundaries += ((b * 64 + p + 1) % 512) == 0; }
            }
            check(count == 1, "each complete block must have one sample");
        }
        for (unsigned count : histogram) check(count > 800 && count < 1250, "all operation positions must be sampled");
        // Exact known workload: cheap operations cost 1, each batch's last draw
        // costs 101. Fixed stride 64 times every expensive draw and estimates
        // 13.5 instead of the real 1 + 100/512. Stratification must recover it.
        const double expected = 1.0 + 100.0 / 512;
        const double estimated = 1.0 + 100.0 * boundaries / blocks;
        std::printf("seed %u: periodic workload real %.6f, estimated %.6f, fixed-stride 13.5\n", seed, expected, estimated);
        check(std::abs(estimated - expected) < 0.06, "periodic batch cost must not alias fixed sampling phase");
    }
    std::puts("profile_sampling_test: PASS");
}
