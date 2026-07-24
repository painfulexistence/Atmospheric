# ─────────────────────────────────────────────────────────────────────────────
# Android cross-builds: synthesize a per-ABI vcpkg triplet before the vcpkg
# toolchain runs. The Gradle externalNativeBuild passes ANDROID_ABI/NDK; this
# maps it to a vcpkg architecture, writes triplets/<arch>-android.cmake, and
# forces VCPKG_TARGET_TRIPLET to it. include()d from the root CMakeLists.txt
# between project() and the vcpkg toolchain include — order matters.
# ─────────────────────────────────────────────────────────────────────────────

if(ANDROID AND DEFINED CMAKE_ANDROID_NDK)
    # Determine the architecture and build triplet for vcpkg
    # VCPKG_ARCH: the architecture name used inside the triplet file (VCPKG_TARGET_ARCHITECTURE)
    # VCPKG_TRIPLET_NAME: the vcpkg-convention triplet name (e.g. arm64, x64 — no underscores)
    set(VCPKG_ARCH "")
    set(VCPKG_TRIPLET_NAME "")
    set(VCPKG_HOST "")
    set(TARGET_ABI "")
    
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "arm64-v8a")
        set(VCPKG_ARCH "arm64")
        set(VCPKG_TRIPLET_NAME "arm64")
        set(VCPKG_HOST "aarch64-linux-android")
        set(TARGET_ABI "arm64-v8a")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64" OR ANDROID_ABI STREQUAL "x86_64")
        set(VCPKG_ARCH "x64")
        set(VCPKG_TRIPLET_NAME "x64")
        set(VCPKG_HOST "x86_64-linux-android")
        set(TARGET_ABI "x86_64")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a" OR ANDROID_ABI STREQUAL "armeabi-v7a")
        set(VCPKG_ARCH "arm")
        set(VCPKG_TRIPLET_NAME "arm")
        set(VCPKG_HOST "arm-linux-androideabi")
        set(TARGET_ABI "armeabi-v7a")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86" OR ANDROID_ABI STREQUAL "x86")
        set(VCPKG_ARCH "x86")
        set(VCPKG_TRIPLET_NAME "x86")
        set(VCPKG_HOST "i686-linux-android")
        set(TARGET_ABI "x86")
    endif()

    if(VCPKG_TRIPLET_NAME)
        set(TRIPLET_CONTENT "")
        string(APPEND TRIPLET_CONTENT "set(VCPKG_TARGET_ARCHITECTURE ${VCPKG_ARCH})\n")
        string(APPEND TRIPLET_CONTENT "set(VCPKG_CRT_LINKAGE dynamic)\n")
        string(APPEND TRIPLET_CONTENT "set(VCPKG_LIBRARY_LINKAGE static)\n")
        string(APPEND TRIPLET_CONTENT "set(VCPKG_CMAKE_SYSTEM_NAME Android)\n")
        string(APPEND TRIPLET_CONTENT "set(VCPKG_CMAKE_SYSTEM_VERSION 28)\n")
        string(APPEND TRIPLET_CONTENT "set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE \"${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake\")\n")
        if(VCPKG_HOST)
            string(APPEND TRIPLET_CONTENT "set(VCPKG_MAKE_BUILD_TRIPLET \"--host=${VCPKG_HOST}\")\n")
        endif()
        string(APPEND TRIPLET_CONTENT "set(VCPKG_CMAKE_CONFIGURE_OPTIONS \"-DANDROID_ABI=${TARGET_ABI}\")\n")
        
        file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/triplets/${VCPKG_TRIPLET_NAME}-android.cmake" "${TRIPLET_CONTENT}")
        message(STATUS "Android CMake: Generated custom ${VCPKG_TRIPLET_NAME}-android triplet targeting ${TARGET_ABI}")
        set(VCPKG_TARGET_TRIPLET "${VCPKG_TRIPLET_NAME}-android" CACHE STRING "" FORCE)
    endif()
endif()
