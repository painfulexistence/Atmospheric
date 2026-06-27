# InstallDependencies.cmake
# Helper script to automatically configure the bundled vcpkg submodule.
#
# Usage in external projects:
#   include(Atmospheric/cmake/InstallDependencies.cmake)
#   project(MyGame LANGUAGES CXX)
#   add_subdirectory(Atmospheric)

# Expose cmake/helpers/ so downstream games can include(AssetPipeline) etc.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/helpers")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain file")
    message(STATUS "[AtmosphericEngine] Automatically using bundled vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Enable manifest mode by default since we have vcpkg.json
set(VCPKG_MANIFEST_MODE ON CACHE BOOL "Enable vcpkg manifest mode")

# Automatically set target triplet for Emscripten/WASM cross-compilation
if(EMSCRIPTEN)
    if(NOT DEFINED VCPKG_TARGET_TRIPLET)
        set(VCPKG_TARGET_TRIPLET "wasm32-emscripten" CACHE STRING "vcpkg target triplet")
        message(STATUS "[AtmosphericEngine] Emscripten detected. Automatically using WASM triplet: ${VCPKG_TARGET_TRIPLET}")
    endif()
    if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
        set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../triplets" CACHE STRING "vcpkg overlay triplets")
        message(STATUS "[AtmosphericEngine] Using custom Emscripten triplet with atomics support: ${VCPKG_OVERLAY_TRIPLETS}")
    endif()
endif()
