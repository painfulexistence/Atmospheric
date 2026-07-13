# Deathmatch: 2-player, server-authoritative, Quake-style arena shooter, built
# to demonstrate lag compensation and the favor-the-shooter vs favor-the-target
# tradeoff — the FPS-lineage netcode HideAndSeek's proximity-tag mechanic never
# needed. See authority.hpp for the design.
#
# Binaries:
#   - DeathmatchClient: windowed renderer + input + ClientNet. Builds natively
#     (connects to a server, or --local) AND for the web, where it is always
#     single-player --local: the embedded authority is reached over an
#     in-process LoopbackDatagramSocket, so no UDP/WebTransport is needed.
#   - DeathmatchServer: DeathmatchAuthority in a standalone process (native only).

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ── DeathmatchClient: windowed renderer + input + ClientNet (native + web) ────
# authority.cpp is compiled in for --local (an embedded DeathmatchAuthority, over
# loopback natively / an in-process LoopbackDatagramSocket on the web).
add_executable(DeathmatchClient main.cpp client_net.cpp authority.cpp)
set_target_properties(DeathmatchClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DeathmatchClient
)
target_include_directories(DeathmatchClient PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/include
)
target_link_libraries(DeathmatchClient PRIVATE
    AtmosphericEngine
    glm::glm
    fmt::fmt
    spdlog::spdlog
)

if(EMSCRIPTEN)
    # ── Web build: single-player --local, no sockets ─────────────────────────
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DeathmatchClient/assets)

    # Preload engine defaults (shaders/fonts/ui) + Deathmatch's scene. NOT the
    # optional 2k HDRI (assets/textures/*.exr): it's guarded by FileSystem::Exists
    # and the sky falls back to a gradient, so it stays out of the wasm bundle.
    add_custom_target(stage_DeathmatchClient_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/shaders ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/fonts ${EM_PRELOAD_STAGING_DIR}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/ui ${EM_PRELOAD_STAGING_DIR}/ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMENT "Staging Emscripten preload assets (shaders, fonts, ui, scenes) for DeathmatchClient"
    )
    add_dependencies(DeathmatchClient stage_DeathmatchClient_em_preload)

    # INVOKE_RUN=0: main() is not auto-run. The shell.html lobby lets the player
    # pick Solo (working) or PvP (WIP) and then launches via Module.callMain,
    # mirroring MultiplayerSandbox. _main is exported so callMain can reach it.
    target_link_options(DeathmatchClient PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sINVOKE_RUN=0
        -sEXPORTED_FUNCTIONS=["_main"]
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/ui@assets/ui"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
        # The lobby is the entry point (INVOKE_RUN=0), so the custom shell is
        # applied in all build types, not just Release.
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    )
else()
    # ── Native build ─────────────────────────────────────────────────────────
    find_package(glad CONFIG REQUIRED)
    target_link_libraries(DeathmatchClient PRIVATE
        glad::glad
        raudio
        SDL3::SDL3
    )
    if(WIN32)
        target_link_libraries(DeathmatchClient PRIVATE ws2_32)
    endif()
endif()

ae_copy_engine_assets(DeathmatchClient)
ae_copy_game_assets(DeathmatchClient "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ── DeathmatchServer: DeathmatchAuthority, no attached player (native only) ────
# No AtmosphericEngine dependency — no GameObject/Scene/rendering, just UdpSocket
# (compiled directly in, zero third-party deps) and spdlog. Same plain-loop
# reasoning as HideAndSeekServer / UdpRelay.
if(NOT EMSCRIPTEN)
    add_executable(DeathmatchServer
        server_main.cpp
        authority.cpp
        ${CMAKE_SOURCE_DIR}/Engine/src/udp_socket.cpp
    )
    set_target_properties(DeathmatchServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DeathmatchServer
    )
    target_include_directories(DeathmatchServer PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
        # udp_socket.cpp uses a quoted #include "udp_socket.hpp" (matching every
        # other Engine/src/*.cpp), which needs this directory on the include path.
        ${CMAKE_SOURCE_DIR}/Engine/include/Atmospheric
    )
    target_link_libraries(DeathmatchServer PRIVATE spdlog::spdlog)
    if(WIN32)
        target_link_libraries(DeathmatchServer PRIVATE ws2_32)
    endif()
endif()
