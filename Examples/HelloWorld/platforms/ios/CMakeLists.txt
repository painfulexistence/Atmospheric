# iOS build — invoke from this directory:
#
#   cmake -G Xcode \
#         -B build \
#         -S Examples/HelloWorld/platforms/ios \
#         -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
#         -DVCPKG_TARGET_TRIPLET=arm64-ios
#
cmake_minimum_required(VERSION 3.21)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

# Both arm64-ios and arm64-ios-simulator vcpkg triplets only build arm64 slices.
# Without this, Xcode defaults to its standard simulator archs (which includes
# x86_64 on the Simulator SDK), and linking fails with missing symbols for the
# x86_64 slice since vcpkg never built its dependencies for it.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

# vcpkg must be loaded before project().
# Set VCPKG_MANIFEST_DIR explicitly so vcpkg finds vcpkg.json in the repo root
# regardless of which directory cmake is invoked from.
set(VCPKG_MANIFEST_DIR "${ROOT_DIR}" CACHE STRING "")
# arm64-ios / arm64-ios-simulator are custom triplets (triplets/*.cmake), not
# community vcpkg triplets — without this, vcpkg can't locate them at all.
list(APPEND VCPKG_OVERLAY_TRIPLETS "${ROOT_DIR}/triplets")
include("${ROOT_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

project(HelloWorld-iOS LANGUAGES C CXX OBJC OBJCXX Swift)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SWIFT_LANGUAGE_VERSION 6.0)

include(FetchContent)

set(AE_ROOT_DIR "${ROOT_DIR}" CACHE PATH "AtmosphericEngine repo root" FORCE)

add_subdirectory("${ROOT_DIR}/Engine" "${CMAKE_BINARY_DIR}/Engine")
add_subdirectory("${ROOT_DIR}/Examples/HelloWorld" "${CMAKE_BINARY_DIR}/HelloWorld")

# ── Xcode / bundle settings ───────────────────────────────────────────────────
set_target_properties(main PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_SWIFT_VERSION "6.0"
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTEROP_MODE "objcxx"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/Sources/AtmosEngine-Bridging-Header.h"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    XCODE_ATTRIBUTE_GCC_PREPROCESSOR_DEFINITIONS "IMGUI_IMPL_OPENGL_ES3=1"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.atmospheric.helloworld"
)

# Code signing: simulator never needs it; device requires a team + automatic profile.
# Apply to BOTH Debug and Release configurations (Xcode treats signing as per-config,
# so Xcode-GUI changes only affect one config — driving it from CMake here keeps both in sync).
if(CMAKE_OSX_SYSROOT STREQUAL "iphonesimulator")
    set_target_properties(main PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED  "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY    ""
    )
else()
    # Device build: pass -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<10-char TeamID> at configure time.
    set_target_properties(main PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE    "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    )
endif()

target_sources(main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Sources/ios_init.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sources/AtmosAppDelegate.swift"
)

target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Sources")

# Swift needs SDL3 public + internal headers to compile the bridging header
target_include_directories(main PRIVATE
    $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
    "${CMAKE_BINARY_DIR}/_deps/sdl3-src/src/video/uikit"
)
