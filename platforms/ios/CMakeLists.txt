# iOS build — invoke as a standalone CMake project (not from root):
#
#   cmake -G Xcode \
#         -B build-ios \
#         -S platforms/ios \
#         -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
#         -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<10-char team ID>
#
# Requirements:
#   - macOS host with Xcode 16+
#   - SDL3 built as a framework (fetched below)
#   - No vcpkg — vcpkg's wasm32-emscripten triplet is web-only; iOS deps
#     (Bullet, glm, etc.) must come from a separate iOS vcpkg triplet or
#     Homebrew arm64e builds.  Stub those out below once ready.

cmake_minimum_required(VERSION 3.19)
project(AtmosLua-iOS LANGUAGES CXX OBJCXX Swift)

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

# ── TODO: add iOS-compatible versions of Bullet, glm, sol2, RmlUi, etc. ──────
# These are fetched from source in AtmosphericEngine/CMakeLists.txt but the
# iOS triplet for vcpkg is not yet configured.  Either:
#   a) Point VCPKG_TARGET_TRIPLET to arm64-ios and add missing portfiles, or
#   b) FetchContent_Declare each dep here with iOS cmake args.

# ── Lua frontend sources ──────────────────────────────────────────────────────
set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../frontends/lua)
set(LUA_SOURCES
    ${LUA_DIR}/main.cpp
    ${LUA_DIR}/lua_application.cpp
    ${LUA_DIR}/scriptable_component.cpp
    ${LUA_DIR}/bindings/bind_core.cpp
    ${LUA_DIR}/bindings/bind_input.cpp
    ${LUA_DIR}/bindings/bind_world.cpp
    ${LUA_DIR}/bindings/bind_graphics.cpp
    ${LUA_DIR}/bindings/bind_physics.cpp
    ${LUA_DIR}/bindings/bind_audio.cpp
)

# ── iOS platform shell ────────────────────────────────────────────────────────
set(IOS_SOURCES
    Sources/ios_init.mm
    Sources/AtmosAppDelegate.swift
    Sources/IAPManager.swift
)

add_executable(AtmosLua-iOS MACOSX_BUNDLE ${LUA_SOURCES} ${IOS_SOURCES})

set_target_properties(AtmosLua-iOS PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_SWIFT_VERSION "6.0"
    # Swift/C++ interop + Obj-C bridging for SDLUIKitDelegate subclass
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTEROP_MODE "objcxx"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/Sources/AtmosEngine-Bridging-Header.h"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"   # iPhone + iPad
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    # GLES3 — same path as the Emscripten build, no desktop glad
    XCODE_ATTRIBUTE_GCC_PREPROCESSOR_DEFINITIONS
        "IMGUI_IMPL_OPENGL_ES3=1"
)

target_include_directories(AtmosLua-iOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AtmosphericEngine/include
    ${LUA_DIR}
)

target_link_libraries(AtmosLua-iOS PRIVATE
    SDL3::SDL3
    "-framework StoreKit"
    "-framework UIKit"
    "-framework Foundation"
    "-framework OpenGLES"
    "-framework GameController"
    "-framework CoreMotion"
)
