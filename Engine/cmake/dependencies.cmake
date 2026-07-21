# ─────────────────────────────────────────────────────────────────────────────
# Engine third-party dependencies — every external the engine consumes, in one
# place, grouped by acquisition mechanism. include()d from Engine/CMakeLists.txt
# in the same directory scope, so relative paths and variables behave as if the
# blocks were still inline.
#
#   Tier 1 — vcpkg manifest (/vcpkg.json): the default. Prebuilt per-triplet
#            artifacts with binary caching. A dependency lives here unless it
#            has a written reason not to.
#   Tier 2 — FetchContent: compiled from source at configure time. Each entry
#            documents why the vcpkg port can't serve it (no port / need the
#            source tree itself / custom per-platform compilation / version
#            pinned independently of the baseline).
#   Tier 3 — vendored (Engine/external/): trees committed into the repo and
#            built as our own code.
#
# FFmpeg is the one deliberate exception to the tiers: an optional *system*
# dependency via pkg-config (see AE_USE_FFMPEG in CMakeLists.txt) — the vcpkg
# ffmpeg port is enormous, so the system install is used instead.
# ─────────────────────────────────────────────────────────────────────────────

# FetchContent_Populate is called directly for lua_src, sol2_src, and
# basis_universal to avoid running their own CMakeLists.txt (which would
# conflict with our custom targets).  Suppress the CMP0169 deprecation warning.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# ═══ Tier 1 — vcpkg manifest ═════════════════════════════════════════════════

find_package(glm CONFIG REQUIRED)
find_package(Tracy CONFIG)
option(AE_USE_TRACY "Enable Tracy profiler instrumentation" OFF)

option(BUILD_BULLET2_DEMOS OFF)
option(BUILD_CPU_DEMOS OFF)
option(BUILD_OPENGL3_DEMOS OFF)
option(BUILD_EXTRAS OFF)
option(BUILD_BULLET_ROBOTICS_EXTRA OFF)
option(BUILD_BULLET_ROBOTICS_GUI_EXTRA OFF)
option(BUILD_UNIT_TESTS OFF)
option(BUILD_LUA_AS_DLL OFF)
find_package(Bullet CONFIG REQUIRED)

# Box2D for 2D physics
find_package(box2d CONFIG REQUIRED)

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(RmlUi REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)

# glad — GL function loader for desktop GL. The web build uses the browser's
# GL and iOS links OpenGLES directly, so neither needs it.
if(NOT EMSCRIPTEN AND NOT IOS)
    find_package(glad CONFIG REQUIRED)
endif()

# curl + libwebsockets — behind the vcpkg "networking" manifest feature (the
# root CMakeLists appends it to VCPKG_MANIFEST_FEATURES before project()).
# Native only: the web build uses emscripten_fetch / Emscripten WebSocket.
if(AE_USE_NETWORKING AND NOT EMSCRIPTEN)
    find_package(CURL REQUIRED)
    find_package(libwebsockets CONFIG REQUIRED)
endif()

# ═══ Tier 2 — FetchContent (each entry must justify itself) ══════════════════

# ── FastNoiseLite (single-header noise library) ────────────────────────────────────────────
# (FetchContent because the vcpkg registry has no fastnoiselite port.)
FetchContent_Declare(
    FastNoiseLite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG        v1.1.1
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_GetProperties(FastNoiseLite)
if(NOT FastNoiseLite_POPULATED)
    FetchContent_Populate(FastNoiseLite)
endif()

# SDL3 — FetchContent, not the vcpkg sdl3 port, for two load-bearing reasons:
#   1. The Android Gradle projects copy SDL's Java scaffolding
#      (android-project/app/src/main/java) straight out of this source tree;
#      the vcpkg port installs only headers + libraries.
#   2. The tag is pinned to what the vendored imgui SDL3 backend supports,
#      independent of whatever the vcpkg baseline ships.
if(NOT EMSCRIPTEN)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.12 # TODO: update to 3.2.14 after upgrading imgui
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()

# ── Lua 5.4 (built from source) ───────────────────────────────────────────────────────────────
# Building from source rather than find_package(lua) because:
#   1. Emscripten must compile Lua to WASM — system lua.a is host-arch only.
#   2. Avoids architecture mismatch on Apple Silicon (arm64 vs x86_64).
#   3. Version is pinned — no dependency on whatever vcpkg/Homebrew installed.
FetchContent_Declare(lua_src
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_GetProperties(lua_src)
if(NOT lua_src_POPULATED)
    FetchContent_Populate(lua_src)
endif()

# Lua's repo has all .c files in the root (no src/ subdir).
# Exclude the standalone interpreter (lua.c) and compiler (luac.c).
file(GLOB LUA_CORE_SOURCES "${lua_src_SOURCE_DIR}/*.c")
list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*(lua|luac)\.c$")

add_library(lua_static STATIC ${LUA_CORE_SOURCES})
target_include_directories(lua_static PUBLIC "${lua_src_SOURCE_DIR}")
if(UNIX AND NOT EMSCRIPTEN AND NOT IOS)
    # Enable POSIX APIs (better file I/O, signals, etc.) on Linux / macOS.
    # Not needed on Windows, Emscripten, or iOS.
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
endif()
if(IOS)
    # LUA_USE_IOS disables os.execute() and other APIs unavailable in the iOS sandbox.
    target_compile_definitions(lua_static PRIVATE LUA_USE_IOS)
endif()
# Suppress warnings from Lua's C code (not our code, not our problem)
if(MSVC)
    target_compile_options(lua_static PRIVATE /W0)
else()
    target_compile_options(lua_static PRIVATE -w)
endif()
if(EMSCRIPTEN)
    target_compile_options(lua_static PRIVATE -pthread)
endif()

# ── sol2 (develop branch, header-only) ──────────────────────────────────────────────────────────────
# v3.3.0 had a bug in optional_implementation.hpp where tl::optional's CRTP
# base 'construct' method is not visible in the reference-type specialisation
# (optional<T&>).  Apple Clang 15+ (Xcode 15 / macOS 14) enforces this more
# strictly and rejects the code.  Fixed in v3.5.0.
#
# We use FetchContent_Populate (not MakeAvailable) to avoid running sol2's own
# CMakeLists.txt, which would call find_package(Lua) and conflict with lua_static.
FetchContent_Declare(sol2_src
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.5.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_GetProperties(sol2_src)
if(NOT sol2_src_POPULATED)
    FetchContent_Populate(sol2_src)
endif()

# sol2 is header-only; create a minimal INTERFACE target manually.
add_library(sol2_iface INTERFACE)
target_include_directories(sol2_iface INTERFACE "${sol2_src_SOURCE_DIR}/include")
# Link lua_static as INTERFACE so that:
#   1. sol2 headers resolve <lua.h> / <lua.hpp> from our Lua 5.4 source,
#      not from any system/vcpkg installation.
#   2. Include order is guaranteed: our lua_static dir comes first.
target_link_libraries(sol2_iface INTERFACE lua_static)
# Alias matches the target name produced by find_package(sol2 CONFIG REQUIRED),
# so existing target_link_libraries(... sol2::sol2) lines continue to work.
add_library(sol2::sol2 ALIAS sol2_iface)

# TinyUSDZ — no vcpkg port exists; pinned to a commit and configured/patched
# below. Target wiring (link/include/defines) stays with the feature block in
# CMakeLists.txt.
if(AE_USE_TINYUSDZ)
    set(TINYUSDZ_WITH_TYDRA ON CACHE BOOL "" FORCE)
    set(TINYUSDZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TINYUSDZ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TINYUSDZ_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    # TinyUSDZ vendors its own tinyexr (src/external/tinyexr.cc) for EXR *texture*
    # decoding inside USD. The engine already links vcpkg's tinyexr (HDR/EXR env
    # maps), so building both yields duplicate-symbol link errors
    # (multiple definition of FreeEXRErrorMessage, …). Disable TinyUSDZ's copy —
    # .exr textures referenced *from USD* are rare, and this is the only overlap.
    set(TINYUSDZ_WITH_EXR OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tinyusdz
        GIT_REPOSITORY https://github.com/lighttransport/tinyusdz.git
        GIT_TAG 11a2d361a9b6a0c4bf8918027233858d80ed17c5
    )
    FetchContent_MakeAvailable(tinyusdz)
    # On Emscripten the engine builds with -pthread (shared memory). Every object
    # linked into the module must then carry the WASM 'atomics'/'bulk-memory'
    # features, or wasm-ld rejects it ("--shared-memory is disallowed by
    # image-loader.cc.o …"). TinyUSDZ's FetchContent targets don't inherit our
    # -pthread, so force it onto them — exactly as lua_static does above. (Tydra
    # is compiled into tinyusdz_static, so this one target covers image-loader.cc.)
    if(EMSCRIPTEN)
        target_compile_options(tinyusdz_static PRIVATE -pthread)
    endif()
endif()

# Basis Universal — FetchContent, not the vcpkg basisu port: only the
# transcoder is compiled (with size-trimming defines for the wasm payload),
# and the host-arch encoder CLI must be built while cross-compiling for
# Emscripten — neither of which the per-triplet port provides.
# ============================================================================
# Basis Universal transcoder (KTX2 GPU-compressed texture support)
# Only the transcoder is compiled — the encoder is not needed at runtime.
# Supported target formats for WebGL2:
#   - ETC2  (GLES3 required, best for web/mobile)
#   - S3TC  (desktop browsers, via extension check)
# Developers convert source textures to KTX2 with:
#   basisu -ktx2 -mipmap texture.png
#   toktx --t2 --encode etc1s --mipmap out.ktx2 input.png
# ============================================================================
if(AE_USE_BASIS_UNIVERSAL)
    FetchContent_Declare(
        basis_universal
        GIT_REPOSITORY https://github.com/BinomialLLC/basis_universal.git
        GIT_TAG        1.16.4          # named release tag — allows GIT_SHALLOW
        GIT_SHALLOW    TRUE
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_GetProperties(basis_universal)
    if(NOT basis_universal_POPULATED)
        # Do NOT process basisu's own CMakeLists.txt (it drags in encoder, etc.)
        FetchContent_Populate(basis_universal)
    endif()

    # ── basisu host encoder (for PNG→KTX2 auto-conversion at build time) ─────
    # The transcoder below is compiled with em++ for WASM; we also need the
    # basisu CLI as a HOST tool to convert project textures during the build.
    # Try a system install first; fall back to building from the fetched source
    # using the default (host) compiler — no Emscripten toolchain involved.
    if(EMSCRIPTEN)
        find_program(_BASISU_SYSTEM basisu)
        if(_BASISU_SYSTEM)
            set(BASISU_EXECUTABLE "${_BASISU_SYSTEM}" CACHE INTERNAL "basisu encoder")
            add_custom_target(basisu_host_tool)
        else()
            set(_BASISU_HOST_DIR ${CMAKE_BINARY_DIR}/_basisu_host)
            # basis_universal's CMakeLists hardcodes output to ${SOURCE_DIR}/bin,
            # so the binary lands in basis_universal_SOURCE_DIR/bin/basisu.
            set(BASISU_EXECUTABLE "${basis_universal_SOURCE_DIR}/bin/basisu"
                CACHE INTERNAL "basisu encoder")
            add_custom_command(
                OUTPUT "${BASISU_EXECUTABLE}"
                COMMAND "${CMAKE_COMMAND}"
                    -B "${_BASISU_HOST_DIR}"
                    -S "${basis_universal_SOURCE_DIR}"
                    -DCMAKE_BUILD_TYPE=Release
                COMMAND "${CMAKE_COMMAND}"
                    --build "${_BASISU_HOST_DIR}" --target basisu --parallel
                COMMENT "Building basisu host encoder"
                VERBATIM
            )
            add_custom_target(basisu_host_tool DEPENDS "${BASISU_EXECUTABLE}")
        endif()
        message(STATUS "basisu encoder: ${BASISU_EXECUTABLE}")
    endif()

    add_library(basisu_transcoder STATIC
        ${basis_universal_SOURCE_DIR}/transcoder/basisu_transcoder.cpp
    )
    target_include_directories(basisu_transcoder PUBLIC
        ${basis_universal_SOURCE_DIR}
        ${basis_universal_SOURCE_DIR}/transcoder
    )
    # Disable formats not needed for web (reduces transcoder binary size)
    target_compile_definitions(basisu_transcoder PUBLIC
        BASISD_SUPPORT_KTX2=1          # Enable KTX2 container
        BASISD_SUPPORT_KTX2_ZSTD=0     # Zstandard supercompression not needed
        BASISD_SUPPORT_PVRTC1=0        # PowerVR only (legacy mobile)
        BASISD_SUPPORT_PVRTC2=0        # PowerVR2 only (legacy mobile)
        BASISD_SUPPORT_FXT1=0          # Intel FXT1 (obsolete)
        BASISD_SUPPORT_ATC=0           # ATI (obsolete)
    )
    if(MSVC)
        target_compile_options(basisu_transcoder PRIVATE /W0)
    else()
        target_compile_options(basisu_transcoder PRIVATE
            -Wno-unused-variable
            -Wno-unused-function
            -Wno-unused-but-set-variable
        )
    endif()
endif()

# ═══ Tier 3 — vendored (Engine/external/) ════════════════════════════════════

# raudio — no vcpkg port exists. Vendored and built by its own CMakeLists; on
# iOS its miniaudio backend pulls in ObjC frameworks, so the sources are
# compiled as Objective-C.
if(NOT EMSCRIPTEN)
    add_subdirectory(external/raudio/projects/CMake)
    if(IOS)
        # miniaudio on Apple platforms includes ObjC frameworks (CoreAudio, AVFoundation).
        # Force Clang to treat raudio.c as Objective-C so those headers compile correctly.
        target_compile_options(raudio PRIVATE -x objective-c)
    endif()
endif()

# imgui — vendored at external/imgui and compiled straight into the engine
# target (see SOURCES_EXT in CMakeLists.txt): the backends are the engine's
# integration surface, and the version is upgraded by hand together with the
# SDL3 pin above.
# stb — vendored single-file headers at external/stb (include dir only).
