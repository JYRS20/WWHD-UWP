# External LLVM project. Build only the embedded frontend, not clang.exe.
add_library(wwhd-clang-bridge SHARED bridge.cpp memory_link.cpp)
target_include_directories(wwhd-clang-bridge PRIVATE
 "${LLVM_MAIN_INCLUDE_DIR}" "${LLVM_BINARY_DIR}/include"
 "${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/include" "${LLVM_BINARY_DIR}/tools/clang/include")
target_compile_features(wwhd-clang-bridge PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(wwhd-clang-bridge PRIVATE /EHsc)
endif()
llvm_map_components_to_libnames(WWHD_LLVM_LIBS native nativecodegen support core passes runtimedyld executionengine object)
target_link_libraries(wwhd-clang-bridge PRIVATE clangCodeGen clangFrontend ${WWHD_LLVM_LIBS})
if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
 target_compile_definitions(wwhd-clang-bridge PRIVATE WINAPI_FAMILY=WINAPI_FAMILY_APP _CRT_APP WWHD_UWP=1)
 target_link_libraries(wwhd-clang-bridge PRIVATE WindowsApp)
 target_link_options(wwhd-clang-bridge PRIVATE /APPCONTAINER)
endif()
