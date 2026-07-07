option(BUILD_SHARED_LIBS OFF)

# Self-sufficient regardless of how this directory is added (Atmospheric's own
# top-level CMakeLists.txt, or a downstream project that bypasses it and calls
# add_subdirectory(Atmospheric/Engine) directly, e.g. platforms/ios).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/helpers")
include(AssetPipeline)


# FetchContent_Populate is called directly for lua_src, sol2_src, and
# basis_universal to avoid running their own CMakeLists.txt (which would
# conflict with our custom targets).  Suppress the CMP0169 deprecation warning.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# ── FastNoiseLite (single-header noise library) ────────────────────────────────────────────
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
    if(NOT IOS)
        find_package(glad CONFIG REQUIRED)
    endif()
endif()
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

# Generate FlatBuffers headers from schema
set(FLATBUFFERS_SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schemas)
set(FLATBUFFERS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${FLATBUFFERS_GENERATED_DIR})

add_custom_command(
    OUTPUT ${FLATBUFFERS_GENERATED_DIR}/Scene_generated.h
    COMMAND flatbuffers::flatc --cpp -o ${FLATBUFFERS_GENERATED_DIR} ${FLATBUFFERS_SCHEMA_DIR}/Scene.fbs
    DEPENDS ${FLATBUFFERS_SCHEMA_DIR}/Scene.fbs
    COMMENT "Generating FlatBuffers headers for Scene"
)

if(NOT EMSCRIPTEN)
    add_subdirectory(external/raudio/projects/CMake)
    if(IOS)
        # miniaudio on Apple platforms includes ObjC frameworks (CoreAudio, AVFoundation).
        # Force Clang to treat raudio.c as Objective-C so those headers compile correctly.
        target_compile_options(raudio PRIVATE -x objective-c)
    endif()
endif()


#file(GLOB SOURCES src/*.cpp RECURSIVE)
set(SOURCES
    src/application.cpp
    src/game_layer.cpp
    src/editor_layer.cpp
    src/subsystem.cpp
    src/console_subsystem.cpp
    src/game_object.cpp
    src/transform_component.cpp
    src/rigidbody_component.cpp
    src/light_component.cpp
    src/camera_component.cpp
    src/mesh.cpp
    src/gl_buffer.cpp
    src/gl_render_target.cpp
    src/gl_pipeline.cpp
    src/csg.cpp
    src/frustum.cpp
    src/mesh_component.cpp
    src/sprite_component.cpp
    src/sprite_3d_component.cpp
    src/text_2d_component.cpp
    src/text_3d_component.cpp
    src/height_field.cpp
    src/terrain_mesh_component.cpp
    src/terrain_streamer.cpp
    src/terrain_tile_cache.cpp
    src/height_field_collider_component.cpp
    src/water_component.cpp
    src/shader.cpp
    src/graphics_subsystem.cpp
    src/graphics_subsystem_tile.cpp
    src/renderer.cpp
    src/physics_subsystem_3d.cpp
    src/physics_debug_drawer.cpp
    src/input_subsystem.cpp
    src/scene.cpp
    src/action.cpp
    src/action_manager.cpp
    src/animator_2d.cpp
    src/asset_manager.cpp
    src/font_manager.cpp
    src/job_system.cpp
    src/bullet_task_scheduler.cpp
    src/particle_subsystem.cpp
    src/particle_emitter.cpp
    src/file.cpp
    src/rmlui_renderer.cpp
    src/rmlui_system.cpp
    src/rmlui_manager.cpp
    src/ui_page_manager.cpp
    src/batch_renderer_2d.cpp
    src/physics_subsystem_2d.cpp
    src/rigidbody_2d_component.cpp
    src/mesh_builder.cpp
    src/shape_renderer_component.cpp
    src/voxel_chunk_component.cpp
    src/voxel_world.cpp
    src/voxel_chunk_pass.cpp
    src/micro_voxel_pass.cpp
    src/voxel_volume_component.cpp
    src/portal_pass.cpp
    src/portal_component.cpp
    src/sun_component.cpp
    src/scene_loader.cpp
    src/scene_transition.cpp
    src/video_player.cpp
    src/video_recorder.cpp
    src/script_subsystem.cpp
    src/component_factory.cpp
    src/tilemap_2d.cpp
    src/lighting_2d.cpp
    ${FLATBUFFERS_GENERATED_DIR}/Scene_generated.h
)
# ── Graphics backend selection ────────────────────────────────────────────────────────────────────
# Native  : Dawn WebGPU (not yet integrated — falls back to OpenGL 4.1).
# Web     : browser WebGPU via Emscripten (AE_USE_WEBGPU=ON)
#           or WebGL 2.0 fallback (AE_USE_WEBGPU=OFF, default).
option(AE_USE_WEBGPU "Target WebGPU on Emscripten builds (replaces WebGL2)" OFF)

list(APPEND SOURCES src/gfx_factory.cpp)
list(APPEND SOURCES src/audio_subsystem.cpp)
if(NOT EMSCRIPTEN)
    list(APPEND SOURCES src/window_sdl3.cpp)
else()
    list(APPEND SOURCES src/window_glfw.cpp)
    # WASM editor bridge frontend (Emscripten-only). Kept in frontends/ for
    # organisational parity with frontends/lua, but compiled into the engine
    # library because the exports are engine infrastructure (the EXPORTED_FUNCTIONS
    # link flags below are likewise engine-level INTERFACE options), not per-game
    # logic like the Lua frontend.
    list(APPEND SOURCES frontends/wasm/bindings.cpp)
    if(AE_USE_WEBGPU)
        list(APPEND SOURCES src/gpu_buffer.cpp)
        list(APPEND SOURCES src/gpu_render_target.cpp)
        list(APPEND SOURCES src/gpu_canvas_pass.cpp)
        list(APPEND SOURCES src/gpu_pipeline.cpp)
    endif()
endif()
# FileSystem: platform-agnostic async I/O with in-process cache.
#   Web    : emscripten_fetch + IndexedDB + MEMFS writes for text assets.
#   Native : synchronous fread + JobSystem-parallel Prefetch.
# Compiled on all platforms (not just Emscripten).
list(APPEND SOURCES src/file_system.cpp)
# UdpSocket: shared non-blocking UDP socket primitive (open/bind/send/recv/
# close + platform shim) underlying UdpRelay, UdpRelayClient, and every
# example's own net code. Same zero-extra-dependency reasoning. Empty TU on
# Emscripten.
list(APPEND SOURCES src/udp_socket.cpp)
# UdpRelay: OS sockets only, no third-party deps — compiled always so server
# builds and the relay loopback test don't require AE_USE_NETWORKING (which
# drags in curl/libwebsockets/OpenSSL). Empty TU on Emscripten.
list(APPEND SOURCES src/udp_relay.cpp)
# UdpRelayClient: client-side counterpart — wraps outgoing datagrams for
# delivery through a UdpRelay. Same zero-extra-dependency reasoning.
list(APPEND SOURCES src/udp_relay_client.cpp)
# RelaySubsystem: thin Application/AddSubsystem<T> wrapper around UdpRelay
# (same zero-extra-dependency reasoning; empty TU on Emscripten).
list(APPEND SOURCES src/relay_subsystem.cpp)

set(SOURCES_EXT
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)
if(EMSCRIPTEN)
    list(APPEND SOURCES_EXT external/imgui/backends/imgui_impl_glfw.cpp)
else()
    list(APPEND SOURCES_EXT external/imgui/backends/imgui_impl_sdl3.cpp)
endif()

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



# ── Networking (HttpClient + WebSocketClient + NetworkSubsystem) ──────────────
# Native  : libcurl (HTTP/2, OpenSSL) + libwebsockets (OpenSSL)
# Web     : emscripten_fetch (already linked via -sFETCH=1) + Emscripten WebSocket
option(AE_USE_NETWORKING "Enable HTTP and WebSocket networking" OFF)
if(AE_USE_NETWORKING)
    list(APPEND SOURCES
        src/http_client.cpp
        src/websocket_client.cpp
        src/network_subsystem.cpp
    )
    if(NOT EMSCRIPTEN)
        find_package(CURL REQUIRED)
        find_package(libwebsockets CONFIG REQUIRED)
        message(STATUS "Networking enabled (libcurl + libwebsockets, OpenSSL, HTTP/2)")
    else()
        message(STATUS "Networking enabled (emscripten_fetch + Emscripten WebSocket)")
    endif()
endif()

add_library(AtmosphericEngine STATIC ${SOURCES} ${SOURCES_EXT})

target_include_directories(AtmosphericEngine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${BULLET_INCLUDE_DIR}
        # lua_static has PUBLIC include dirs → Lua headers propagate automatically
        ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Atmospheric
        ${FLATBUFFERS_GENERATED_DIR}
        ${fastnoiselite_SOURCE_DIR}/Cpp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_precompile_headers(AtmosphericEngine PRIVATE src/pch.hpp)
target_link_libraries(AtmosphericEngine
    PUBLIC
        glm::glm fmt::fmt BulletSoftBody BulletDynamics BulletCollision LinearMath
        lua_static sol2::sol2
        RmlUi::RmlUi box2d::box2d flatbuffers::flatbuffers
)
if(NOT EMSCRIPTEN)
    target_link_libraries(AtmosphericEngine PUBLIC raudio)
endif()
if(AE_USE_BASIS_UNIVERSAL)
    target_link_libraries(AtmosphericEngine PUBLIC basisu_transcoder)
    target_compile_definitions(AtmosphericEngine PUBLIC AE_USE_BASIS_UNIVERSAL)
endif()
if(AE_USE_NETWORKING)
    target_compile_definitions(AtmosphericEngine PUBLIC AE_USE_NETWORKING)
    if(NOT EMSCRIPTEN)
        target_link_libraries(AtmosphericEngine PUBLIC CURL::libcurl websockets)
    else()
        # -sWEBSOCKET=1 exposes emscripten/websocket.h and the JS WebSocket glue.
        target_compile_options(AtmosphericEngine PUBLIC -sWEBSOCKET=1)
        target_link_options(AtmosphericEngine INTERFACE -sWEBSOCKET=1)
    endif()
endif()
if(AE_USE_FFMPEG AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    # Use the system FFmpeg located via pkg-config rather than building the
    # heavy ffmpeg port through vcpkg. Install it with:
    #   macOS:        brew install ffmpeg pkg-config
    #   Debian/Ubuntu: apt install libavcodec-dev libavformat-dev libavutil-dev \
    #                              libswscale-dev libswresample-dev pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET
            libavcodec libavformat libavutil libswscale libswresample)
    endif()
    if(FFMPEG_FOUND)
        message(STATUS "FFmpeg enabled (AE_USE_FFMPEG=ON, system via pkg-config). VideoPlayer and VideoRecorder available.")
        target_compile_definitions(AtmosphericEngine PUBLIC AE_HAS_FFMPEG)
        target_link_libraries(AtmosphericEngine PUBLIC PkgConfig::FFMPEG)
    else()
        message(WARNING
            "AE_USE_FFMPEG=ON but no system FFmpeg was found via pkg-config. Install it "
            "(macOS: `brew install ffmpeg pkg-config`; Debian/Ubuntu: `apt install "
            "libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev "
            "pkg-config`), or pass -DAE_USE_FFMPEG=OFF. VideoPlayer and VideoRecorder are disabled.")
    endif()
elseif(NOT AE_USE_FFMPEG)
    message(STATUS "FFmpeg disabled (AE_USE_FFMPEG=OFF). VideoPlayer and VideoRecorder will be unavailable.")
endif()
if(AE_USE_TINYUSDZ)
    # TinyUSDZ: dependency-free USD (USDA/USDC/USDZ) reader plus its Tydra
    # render-data converter, vendored as a git submodule under external/. It's a
    # heavy dependency, so it lives behind an option (default OFF) to keep the
    # baseline build small per the project vision. The static target's include
    # dir is only exposed PRIVATE, so add external/tinyusdz/src ourselves for
    # <tinyusdz.hh> / <tydra/render-data.hh>.
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/tinyusdz/CMakeLists.txt)
        message(FATAL_ERROR
            "AE_USE_TINYUSDZ=ON but external/tinyusdz is empty. Run "
            "`git submodule update --init --recursive Engine/external/tinyusdz`.")
    endif()
    set(TINYUSDZ_WITH_TYDRA ON CACHE BOOL "" FORCE)
    set(TINYUSDZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TINYUSDZ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TINYUSDZ_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/tinyusdz EXCLUDE_FROM_ALL)
    target_link_libraries(AtmosphericEngine PUBLIC tinyusdz::tinyusdz_static)
    target_include_directories(AtmosphericEngine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/tinyusdz/src)
    target_compile_definitions(AtmosphericEngine PUBLIC AE_USE_TINYUSDZ)
    message(STATUS "TinyUSDZ enabled (AE_USE_TINYUSDZ=ON). AssetManager::LoadUSD available.")
endif()
if(EMSCRIPTEN OR ANDROID OR IOS)
    # IMGUI_IMPL_OPENGL_ES3 : tell Dear ImGui's OpenGL3 backend to use GLES3
    #   headers instead of the default desktop GL3 loader.
    #   Required on WebGL 2.0, Android (GLES3), and iOS (OpenGLES).
    target_compile_definitions(AtmosphericEngine PRIVATE IMGUI_IMPL_OPENGL_ES3)
endif()
if(EMSCRIPTEN)
    # spdlog::spdlog : console_subsystem.cpp and scene_loader.cpp call spdlog directly;
    #   Emscripten maps stdout → browser console so spdlog works fine on web.
    target_compile_definitions(AtmosphericEngine PUBLIC BT_THREADSAFE=1)
    target_link_libraries(AtmosphericEngine PUBLIC spdlog::spdlog)
    # -pthread        : enable WASM threads (SharedArrayBuffer + Atomics)
    # -sFETCH=1       : enable emscripten_fetch (XHR + IndexedDB persistence)
    #                   used by FileSystem (web platform implementation)
    # -sUSE_PTHREADS=1              : compile with pthread support (Web Workers)
    # -sPTHREAD_POOL_SIZE           : pre-spawn workers to avoid first-use latency
    # -sPROXY_TO_PTHREAD=1          : run main() on a worker so blocking Wait()
    #                                 calls do not freeze the browser main thread
    target_compile_options(AtmosphericEngine PUBLIC -pthread)
    if(AE_USE_WEBGPU)
        target_compile_definitions(AtmosphericEngine PUBLIC AE_USE_WEBGPU)
        target_compile_options(AtmosphericEngine PUBLIC --use-port=emdawnwebgpu)
        # The actual backend is chosen at runtime by GfxFactory::Init():
        #   - browser exposes navigator.gpu → attempts WebGPU
        #   - WebGPU unavailable or device creation fails → falls back to WebGL 2
        # WebGL 2 flags are therefore kept alongside emdawnwebgpu so the
        # OpenGL fallback path and ImGui (still imgui_impl_opengl3) continue
        # to work at runtime.
        # --closure=1 is recommended in release builds to reduce emdawnwebgpu JS size.
        # TODO: switch ImGui backend to imgui_impl_wgpu once WebGPU path is stable.
        target_link_options(AtmosphericEngine INTERFACE
            -sWASM=1 -O3 -sUSE_GLFW=3
            --use-port=emdawnwebgpu
            -sASYNCIFY=1
            -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
            -sFETCH=1
            -sUSE_PTHREADS=1
            -sINITIAL_MEMORY=134217728
            -sMAXIMUM_MEMORY=536870912
            "-sPTHREAD_POOL_SIZE=(/Mobi|Android|iPhone|iPad/i.test(navigator.userAgent) ? Math.min(4, navigator.hardwareConcurrency || 2) : Math.min(16, navigator.hardwareConcurrency || 4))"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_ae_load_editor_scene','_ae_load_editor_scene_json','_ae_add_scene','_ae_unload_scene','_ae_get_loaded_scenes','_ae_get_last_loaded_scene','_ae_reset_scene','_ae_get_version','_ae_is_ready','_ae_mount_file','_ae_get_scene_error']"
            "-sEXPORTED_RUNTIME_METHODS=['requestFullscreen','ccall','HEAPU8','callMain']"
            "SHELL:--post-js ${CMAKE_CURRENT_SOURCE_DIR}/src/post.js")
    else()
        target_link_options(AtmosphericEngine INTERFACE
            -sWASM=1 -O3 -sUSE_GLFW=3
            -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
            -sFETCH=1
            -sUSE_PTHREADS=1
            -sINITIAL_MEMORY=134217728
            -sMAXIMUM_MEMORY=536870912
            "-sPTHREAD_POOL_SIZE=(/Mobi|Android|iPhone|iPad/i.test(navigator.userAgent) ? Math.min(4, navigator.hardwareConcurrency || 2) : Math.min(16, navigator.hardwareConcurrency || 4))"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_ae_load_editor_scene','_ae_load_editor_scene_json','_ae_add_scene','_ae_unload_scene','_ae_get_loaded_scenes','_ae_get_last_loaded_scene','_ae_reset_scene','_ae_get_version','_ae_is_ready','_ae_mount_file','_ae_get_scene_error']"
            "-sEXPORTED_RUNTIME_METHODS=['requestFullscreen','ccall','HEAPU8','callMain']"
            "SHELL:--post-js ${CMAKE_CURRENT_SOURCE_DIR}/src/post.js")
    endif()
else()
    target_compile_definitions(AtmosphericEngine PUBLIC BT_THREADSAFE=1)
    if(NOT IOS)
        target_link_libraries(AtmosphericEngine PUBLIC glad::glad spdlog::spdlog)
    endif()
    if(AE_USE_TRACY)
        target_link_libraries(AtmosphericEngine PUBLIC Tracy::TracyClient)
        target_compile_definitions(AtmosphericEngine PUBLIC TRACY_ENABLE TRACY_VERBOSE TRACY_ON_DEMAND)
    endif()
    target_link_libraries(AtmosphericEngine PUBLIC SDL3::SDL3)
    if(WIN32)
        # Winsock2 for UdpRelay (compiled into the engine unconditionally).
        target_link_libraries(AtmosphericEngine PUBLIC ws2_32)
    endif()
    if(ANDROID)
        find_library(GLES3_LIB GLESv3)
        if(GLES3_LIB)
            target_link_libraries(AtmosphericEngine PUBLIC ${GLES3_LIB})
        else()
            target_link_libraries(AtmosphericEngine PUBLIC GLESv3)
        endif()
        target_link_libraries(AtmosphericEngine PUBLIC log android)
    endif()
    if(IOS)
        target_link_libraries(AtmosphericEngine PUBLIC
            "-framework OpenGLES"
            "-framework UIKit"
            "-framework Foundation"
            "-framework GameController"
            "-framework CoreMotion"
        )
    endif()
endif()

# ── @atmospheric/engine npm package (Emscripten-only) ────────────────────────
# Minimal Application shell (module.cpp); all scene loading driven by the
# JS bridge (bindings.cpp, compiled into AtmosphericEngine above).
#
# Build layout — option C:
#   source  Engine/frontends/wasm/   package.json, index.d.ts (source-of-truth)
#   build   build/AtmosWasm/          atmos.js/.wasm/.worker.js + metadata
#
# POST_BUILD copies package.json + index.d.ts into the build dir so that
# directory is a self-contained npm package for cloudscape's file:// dep:
#   "@atmospheric/engine": "file:../atmospheric-src/build/AtmosWasm"
if(EMSCRIPTEN)
    set(_AE_WASM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/frontends/wasm")
    set(_AE_WASM_OUT "${CMAKE_BINARY_DIR}/AtmosWasm")

    add_executable(atmospheric frontends/wasm/module.cpp)
    target_link_libraries(atmospheric PRIVATE AtmosphericEngine)
    set_target_properties(atmospheric PROPERTIES
        OUTPUT_NAME atmos
        RUNTIME_OUTPUT_DIRECTORY "${_AE_WASM_OUT}"
    )
    # Bundle default_assets/ into atmos.data so the engine can read shaders,
    # fonts and textures from MEMFS at path assets/ without any JS-side setup.
    target_link_options(atmospheric PRIVATE
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/default_assets@assets"
    )

    # The pthread build (-sUSE_PTHREADS=1) emits atmos.worker.js next to atmos.js.
    # Emscripten writes it to $<TARGET_FILE_DIR:atmospheric>; guard the copy so
    # non-pthread builds (where the worker file does not exist) don't error out.
    add_custom_command(TARGET atmospheric POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_AE_WASM_OUT}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_AE_WASM_SRC}/package.json" "${_AE_WASM_OUT}/package.json"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_AE_WASM_SRC}/index.d.ts"  "${_AE_WASM_OUT}/index.d.ts"
        COMMAND ${CMAKE_COMMAND} -Dsrc="$<TARGET_FILE_DIR:atmospheric>/atmos.worker.js"
            -Ddst="${_AE_WASM_OUT}/atmos.worker.js"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/copy_if_exists.cmake"
        COMMENT "Assembling @atmospheric/engine package → ${_AE_WASM_OUT}"
    )
endif()

# ── Headless smoke test ────────────────────────────────────────────────────────
# Boots a full Application with AppConfig::headless — no window, no GL — and
# verifies the fixed-tick loop drives OnUpdate and shuts down cleanly.
# Built so it stays compiling, but NOT registered with ctest (no add_test):
# this PR is scoped to networking, and the headless path needs more shakeout
# before it gates CI. Register it once that's done.
# Gated on AE_BUILD_EXAMPLES so downstream projects consuming Engine directly
# via add_subdirectory don't grow an extra target.
if(AE_BUILD_EXAMPLES AND NOT EMSCRIPTEN)
    add_executable(HeadlessSmokeTest tests/headless_smoke_test.cpp)
    target_link_libraries(HeadlessSmokeTest PRIVATE AtmosphericEngine)
endif()

set(AE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/default_assets" PARENT_SCOPE)
