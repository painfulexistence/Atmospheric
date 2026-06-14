# Wrapper toolchain for Emscripten builds that require WASM shared-memory
# (i.e. -sUSE_PTHREADS=1 / -sPROXY_TO_PTHREAD=1 at link time).
#
# Chains the real Emscripten toolchain first, then appends -matomics and
# -mbulk-memory so that every compiled object file carries the atomics
# feature.  Without these flags wasm-ld rejects --shared-memory at link
# time because the prebuilt vcpkg libraries lack the feature bit.

if(DEFINED ENV{EMSDK})
    set(_EM_TOOLCHAIN "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
elseif(DEFINED ENV{EMSCRIPTEN})
    set(_EM_TOOLCHAIN "$ENV{EMSCRIPTEN}/cmake/Modules/Platform/Emscripten.cmake")
endif()

if(_EM_TOOLCHAIN AND EXISTS "${_EM_TOOLCHAIN}")
    include("${_EM_TOOLCHAIN}")
endif()

# Append after Emscripten.cmake so it cannot be overwritten by that file's
# CMAKE_C_FLAGS_INIT / CMAKE_CXX_FLAGS_INIT assignments.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -matomics -mbulk-memory" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -matomics -mbulk-memory" CACHE STRING "" FORCE)
