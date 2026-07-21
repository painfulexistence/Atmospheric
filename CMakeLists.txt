cmake_minimum_required(VERSION 3.19)

option(AE_USE_BASIS_UNIVERSAL "Enable Basis Universal transcoder for KTX2 GPU-compressed textures" OFF)
option(AE_USE_FFMPEG "Enable FFmpeg for VideoPlayer and VideoRecorder (uses the system FFmpeg found via pkg-config)" ON)
option(AE_BUILD_EXAMPLES "Build the bundled example projects" ON)
option(AE_USE_NETWORKING "Enable HTTP and WebSocket networking" OFF)
option(AE_USE_TINYUSDZ "Enable TinyUSDZ so USD (USDA/USDC/USDZ) is a first-class import format alongside glTF and .map. On by default on every platform — TinyUSDZ also compiles to WebAssembly (upstream: no threads/SharedArrayBuffer/SIMD needed). Turn OFF to skip fetching/compiling it (e.g. -DAE_USE_TINYUSDZ=OFF on the web configure to trim the WASM payload)." ON)

# curl + libwebsockets live behind the "networking" vcpkg manifest feature so
# that default builds don't compile OpenSSL and friends. Must be requested
# before project() — that's when the vcpkg toolchain runs the install.
# (Emscripten builds are excluded by the "platform" gates inside vcpkg.json.)
if(AE_USE_NETWORKING)
    list(APPEND VCPKG_MANIFEST_FEATURES networking)
endif()

# Custom triplets (Android, iOS, etc.) override vcpkg community triplets.
# Must be set before vcpkg.cmake is included.
list(APPEND VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/triplets")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/helpers")

if(VCPKG_TARGET_TRIPLET STREQUAL "wasm32-emscripten" AND NOT VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    if(DEFINED ENV{EMSDK})
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
            "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
            CACHE STRING "Emscripten toolchain (auto-detected from EMSDK)" FORCE)
    endif()
endif()

project(Atmospheric VERSION 0.2)

# Android cross-builds synthesize a per-ABI vcpkg triplet before the vcpkg
# toolchain include below — see cmake/AndroidVcpkgTriplet.cmake.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/AndroidVcpkgTriplet.cmake")

# Programmatically load vcpkg to support Android/iOS native dependency installation
include("${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

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
# FIXME: `!MSVC` is not CMake syntax, so this condition is never true and the
# flag below has never applied. Fixing it is a behavior change (the warning is
# Clang-only; GCC would reject the flag) — tracked as a follow-up, not here.
if(!MSVC)
    add_compile_options(-Werror=reorder-init-list)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(FetchContent)

# Must precede add_subdirectory(Engine): add_test() calls inside a directory
# only register with ctest when testing was enabled before that directory was
# added (Examples' RelayLoopback; Engine currently builds HeadlessSmokeTest
# but doesn't register it as a test — see Engine/CMakeLists.txt).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND AE_BUILD_EXAMPLES)
    enable_testing()
endif()

add_subdirectory(Engine)

# Propagate AE_ASSETS_DIR to the consumer's scope when used as a submodule.
# Engine/CMakeLists.txt sets it with PARENT_SCOPE (→ this scope); re-export it
# one more level so that ae_copy_engine_assets() works in the caller's CMakeLists.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(AE_ASSETS_DIR "${AE_ASSETS_DIR}" PARENT_SCOPE)
endif()

# Only build the bundled examples when Atmospheric is the top-level project
# AND AE_BUILD_EXAMPLES is ON. When consumed via add_subdirectory() (e.g. as
# a submodule), downstream projects pull in Engine alone and provide their
# own executables. Pass -DAE_BUILD_EXAMPLES=OFF to skip examples (e.g. when
# only the engine WASM package is needed for editor deployment).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND AE_BUILD_EXAMPLES)
    add_subdirectory(Examples)
endif()
