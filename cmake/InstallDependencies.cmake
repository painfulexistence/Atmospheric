# InstallDependencies.cmake
# Helper script to automatically configure the bundled vcpkg submodule.
#
# Usage in external projects:
#   include(Atmospheric/cmake/InstallDependencies.cmake)
#   project(MyGame LANGUAGES CXX)
#   add_subdirectory(Atmospheric)

# Custom triplets (Android, iOS, Emscripten, etc.) override vcpkg community triplets.
# Must be set before vcpkg.cmake is included/chainloaded.
list(APPEND VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../triplets")

# Defaults to the engine's own manifest (everything Atmospheric itself needs
# to build). Downstream projects that require additional packages should
# set(VCPKG_MANIFEST_DIR ...) to their own vcpkg.json BEFORE including this
# file — vcpkg only resolves one manifest per configure, so that manifest
# must then also list these same packages.
if(NOT DEFINED VCPKG_MANIFEST_DIR)
    set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE STRING "vcpkg manifest directory")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain file")
    message(STATUS "[AtmosphericEngine] Automatically using bundled vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Enable manifest mode by default since we have vcpkg.json
set(VCPKG_MANIFEST_MODE ON CACHE BOOL "Enable vcpkg manifest mode")

# When targeting the wasm32-emscripten triplet, chain the Emscripten toolchain
# through vcpkg. The EMSDK env var is exported automatically by `source emsdk_env.sh`.
# Checked against VCPKG_TARGET_TRIPLET rather than the EMSCRIPTEN variable, since
# this file runs before project() — EMSCRIPTEN isn't set until enable_language().
if(VCPKG_TARGET_TRIPLET STREQUAL "wasm32-emscripten" AND NOT VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    if(DEFINED ENV{EMSDK})
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
            "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
            CACHE STRING "Emscripten toolchain (auto-detected from EMSDK)" FORCE)
        message(STATUS "[AtmosphericEngine] Emscripten detected via EMSDK env var. Chainloading: ${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
    endif()
endif()
