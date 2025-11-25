cmake_minimum_required(VERSION 3.19)

option(AE_USE_EMSCRIPTEN "Use Emscripten to build a WebAssembly module" OFF)
option(AE_USE_SDL3 "Use SDL3" ON)
option(AE_USE_SDL2 "Use SDL2" OFF)
option(AE_USE_AUDIO "Use audio" ON)

if (AE_USE_EMSCRIPTEN)
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE $ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake CACHE STRING "Emscripten toolchain file")
    set(VCPKG_TARGET_TRIPLET wasm32-emscripten)
    message(STATUS "Built with Emscripten")
endif()


project(AtmosphericEngine VERSION 0.1)

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

add_subdirectory(AtmosphericEngine)
add_subdirectory(Example_HelloWorld)
