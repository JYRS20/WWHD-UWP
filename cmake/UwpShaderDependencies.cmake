# Khronos SPIRV-Cross: Apache-2.0 (see fetched LICENSE and LICENSES/).
# Pinned to the official vulkan-sdk-1.4.321.0 commit. No Vulkan loader/device is required.
include(FetchContent)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(wwhd_spirv_cross
 URL https://codeload.github.com/KhronosGroup/SPIRV-Cross/tar.gz/d8e3e2b141b8c8a167b2e3984736a6baacff316c
 URL_HASH SHA256=18e4abb4ae97451c3d90bb23eb1c22859c8ef08bd5d8d3ec09e035846bbf6556
 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wwhd_spirv_cross)
add_library(wwhd_d3d12_shader_translation STATIC "${WWHD_ROOT}/runtime/src/gfx/d3d12/spirv_hlsl.cpp")
target_compile_features(wwhd_d3d12_shader_translation PUBLIC cxx_std_20)
target_include_directories(wwhd_d3d12_shader_translation PUBLIC "${WWHD_ROOT}/runtime/src")
target_link_libraries(wwhd_d3d12_shader_translation PRIVATE spirv-cross-hlsl)

add_library(wwhd_d3d12_pipeline_layout STATIC "${WWHD_ROOT}/runtime/src/gfx/d3d12/pipeline_layout.cpp")
target_compile_features(wwhd_d3d12_pipeline_layout PUBLIC cxx_std_20)
target_include_directories(wwhd_d3d12_pipeline_layout PUBLIC "${WWHD_ROOT}/runtime/src")
target_link_libraries(wwhd_d3d12_pipeline_layout PUBLIC d3d12 WindowsApp)

add_library(wwhd_d3d12_texture_upload STATIC "${WWHD_ROOT}/runtime/src/gfx/d3d12/texture_upload.cpp"
 "${WWHD_ROOT}/runtime/src/gfx/d3d12/bc45_decode.cpp")
target_compile_features(wwhd_d3d12_texture_upload PUBLIC cxx_std_20)
target_include_directories(wwhd_d3d12_texture_upload PUBLIC "${WWHD_ROOT}/runtime/src")
target_link_libraries(wwhd_d3d12_texture_upload PUBLIC d3d12 WindowsApp)

add_library(wwhd_d3d12_draw_commands STATIC "${WWHD_ROOT}/runtime/src/gfx/d3d12/draw_geometry.cpp"
 "${WWHD_ROOT}/runtime/src/gfx/d3d12/draw_commands.cpp")
target_compile_features(wwhd_d3d12_draw_commands PUBLIC cxx_std_20)
target_include_directories(wwhd_d3d12_draw_commands PUBLIC "${WWHD_ROOT}/runtime/src")
target_link_libraries(wwhd_d3d12_draw_commands PUBLIC d3d12 WindowsApp)

add_library(wwhd_d3d12_descriptors STATIC "${WWHD_ROOT}/runtime/src/gfx/d3d12/descriptor_snapshot.cpp")
target_compile_features(wwhd_d3d12_descriptors PUBLIC cxx_std_20)
target_include_directories(wwhd_d3d12_descriptors PUBLIC "${WWHD_ROOT}/runtime/src")
target_link_libraries(wwhd_d3d12_descriptors PUBLIC wwhd_d3d12_pipeline_layout)
