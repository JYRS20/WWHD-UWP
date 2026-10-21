#pragma once
#include <cstdint>
// All paths are UTF-8. Keys are read locally and never reported through callbacks.
namespace wwhd_extract {
using Progress = bool (*)(void*, uint64_t, uint64_t); // false cancels
using Report = void (*)(void*, const char*);
int extract_disc(const char* image, const char* disc_key, const char* common_key,
                 const char* output, void* context, Progress progress, Report report) noexcept;
}
