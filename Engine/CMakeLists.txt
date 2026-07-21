option(BUILD_SHARED_LIBS OFF)

# Self-sufficient regardless of how this directory is added (Atmospheric's own
# top-level CMakeLists.txt, or a downstream project that bypasses it and calls
# add_subdirectory(Atmospheric/Engine) directly, e.g. platforms/ios).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/helpers")
include(AssetPipeline)


# All third-party acquisition — vcpkg finds, FetchContent source builds, and
# the vendored trees — lives in one tiered, per-dependency-justified file.
# Feature-gated wiring (networking, FFmpeg, TinyUSDZ) stays below, next to the
# feature blocks that own it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake)

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
    src/material.cpp
    src/mesh.cpp
    src/gl_buffer.cpp
    src/gl_render_target.cpp
    src/gl_pipeline.cpp
    src/csg.cpp
    src/frustum.cpp
    src/mesh_renderer_component.cpp
    src/mesh_instancer_component.cpp
    src/sprite_component.cpp
    src/sprite_3d_component.cpp
    src/text_2d_component.cpp
    src/text_3d_component.cpp
    src/height_field.cpp
    src/terrain_mesh_component.cpp
    src/terrain_streamer.cpp
    src/streaming_terrain_component.cpp
    src/terrain_tile_cache.cpp
    src/terrain_texture_gen.cpp
    src/height_field_collider_component.cpp
    src/water_component.cpp
    src/vat.cpp
    src/vat_component.cpp
    src/shader.cpp
    src/graphics_subsystem.cpp
    src/graphics_subsystem_tile.cpp
    src/renderer.cpp
    src/physics_subsystem_3d.cpp
    src/physics_debug_drawer.cpp
    src/input_subsystem.cpp
    src/scene.cpp
    src/easing.cpp
    src/animation_clip.cpp
    src/animator_component.cpp
    src/animation_subsystem.cpp
    src/flipbook_component.cpp
    src/action_timeline_component.cpp
    src/skeletal_component.cpp
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
    src/ui_page_component.cpp
    src/batch_renderer_2d.cpp
    src/physics_subsystem_2d.cpp
    src/rigidbody_2d_component.cpp
    src/mesh_builder.cpp
    src/prefab.cpp
    src/prefab_map.cpp
    src/prefab_gltf.cpp
    src/prefab_usd.cpp
    src/shape_renderer_component.cpp
    src/voxel_chunk_component.cpp
    src/voxel_world.cpp
    src/voxel_world_component.cpp
    src/voxel_chunk_pass.cpp
    src/micro_voxel_pass.cpp
    src/voxel_gi.cpp
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
# NetConditioner: inbound-datagram link emulator (latency/jitter/loss/dup) for
# exercising realtime netcode off the loopback. Pure logic, no sockets — not
# empty on Emscripten, and dep-free so headless servers can use it too.
# (NetMetrics is header-only — Engine/include/Atmospheric/net_metrics.hpp.)
list(APPEND SOURCES src/net_conditioner.cpp)
# LoopbackDatagramSocket: in-process DatagramSocket impl (no OS sockets) for
# --local single-player on the web, where there is no raw UDP. Dep-free, pure
# logic — compiled on all platforms (it's what DatagramSocket aliases to on web).
list(APPEND SOURCES src/loopback_datagram_socket.cpp)
# NetHud: the shared netgraph renderer (DrawNetHud) for the realtime examples.
# Graphics-side (needs GraphicsSubsystem), so part of the engine target only —
# unlike the primitives above, not for the dep-free servers.
list(APPEND SOURCES src/net_hud.cpp)
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

# (Basis Universal transcoder targets come from cmake/dependencies.cmake.)



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
        # curl / libwebsockets are found in cmake/dependencies.cmake (tier 1).
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
        unofficial::tinyexr::tinyexr
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
    # TinyUSDZ itself is fetched in cmake/dependencies.cmake (tier 2 — no
    # vcpkg port); this block only wires the resulting targets in.
    target_link_libraries(AtmosphericEngine PUBLIC tinyusdz::tinyusdz_static)
    target_include_directories(AtmosphericEngine PRIVATE ${tinyusdz_SOURCE_DIR}/src)
    target_compile_definitions(AtmosphericEngine PUBLIC AE_USE_TINYUSDZ)
    message(STATUS "TinyUSDZ enabled (fetched). USD import (LoadUSD/ImportUSDPrefab) available.")
else()
    message(STATUS "TinyUSDZ disabled (AE_USE_TINYUSDZ=OFF) — USD import falls back to the warn-stub.")
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
