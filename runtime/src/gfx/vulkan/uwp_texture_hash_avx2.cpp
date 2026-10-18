// Isolated AVX2 translation unit: do not enable AVX2 for the whole runtime.
#ifdef WWHD_UWP
#include "uwp_texture_hash.h"
#define XXH_INLINE_ALL
#define XXH_VECTOR XXH_AVX2
#include "../../../third_party/xxhash/xxhash.h"
namespace gfxvk {
uint64_t uwp_texture_hash_avx2(const void* bytes, size_t size) {
    return XXH3_64bits(bytes,size);
}
}
#endif
