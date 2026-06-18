# iOS build — invoke as a standalone CMake project (not from root):
#
#   cmake -G Xcode \
#         -B build-ios \
#         -S Examples/HelloWorld/platforms/ios \
#         -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
#
cmake_minimum_required(VERSION 3.19)
project(HelloWorld-iOS LANGUAGES CXX OBJCXX Swift)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SWIFT_LANGUAGE_VERSION 6.0)

include(FetchContent)

# ── SDL3 ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.12
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TESTS  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# ── C++ Sources & Paths ──────────────────────────────────────────────────────
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine)
set(GAME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# For MVP, we collect Engine C++ files
file(GLOB_RECURSE ENGINE_SOURCES 
    "${ENGINE_DIR}/src/*.cpp"
)

set(GAME_SOURCES
    ${GAME_DIR}/main.cpp
)

# ── iOS platform shell ────────────────────────────────────────────────────────
set(IOS_SOURCES
    Sources/ios_init.mm
    Sources/AtmosAppDelegate.swift
)

add_executable(HelloWorld-iOS MACOSX_BUNDLE 
    ${ENGINE_SOURCES} 
    ${GAME_SOURCES} 
    ${IOS_SOURCES}
)

set_target_properties(HelloWorld-iOS PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_SWIFT_VERSION "6.0"
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTEROP_MODE "objcxx"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/Sources/AtmosEngine-Bridging-Header.h"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"   # iPhone + iPad
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    XCODE_ATTRIBUTE_GCC_PREPROCESSOR_DEFINITIONS
        "IMGUI_IMPL_OPENGL_ES3=1"
)

target_include_directories(HelloWorld-iOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources
    ${ENGINE_DIR}/include
    ${ENGINE_DIR}/external/stb
    ${ENGINE_DIR}/external/imgui
    ${GAME_DIR}
)

target_link_libraries(HelloWorld-iOS PRIVATE
    SDL3::SDL3
    "-framework UIKit"
    "-framework Foundation"
    "-framework OpenGLES"
    "-framework GameController"
    "-framework CoreMotion"
)
