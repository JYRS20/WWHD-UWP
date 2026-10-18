// Full-byte XXH3 fast path. Only call after CPU/OS AVX2 capability detection.
#pragma once
#include <cstddef>
#include <cstdint>
namespace gfxvk {
uint64_t uwp_texture_hash_avx2(const void* bytes, size_t size);
}
