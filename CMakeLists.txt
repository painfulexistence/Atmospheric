cmake_minimum_required(VERSION 3.19)

option(AE_USE_BASIS_UNIVERSAL "Enable Basis Universal transcoder for KTX2 GPU-compressed textures" OFF)

if(VCPKG_TARGET_TRIPLET STREQUAL "wasm32-emscripten" AND NOT VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    if(DEFINED ENV{EMSDK})
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
            "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
            CACHE STRING "Emscripten toolchain (auto-detected from EMSDK)" FORCE)
    endif()
endif()

project(Atmospheric VERSION 0.2)

if(EMSCRIPTEN)
    set(AE_USE_BASIS_UNIVERSAL ON CACHE BOOL "Enabled automatically for Emscripten builds" FORCE)
    message(STATUS "Building for Emscripten / WebAssembly")
endif()

if(AE_USE_BASIS_UNIVERSAL)
    message(STATUS "Basis Universal / KTX2 texture compression enabled")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(!MSVC)
    add_compile_options(-Werror=reorder-init-list)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(FetchContent)


add_subdirectory(Engine)
add_subdirectory(Examples)