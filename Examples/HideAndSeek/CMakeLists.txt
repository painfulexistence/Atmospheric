# HideAndSeek: server-authoritative 1 Seeker vs 1 Hider hide-and-tag,
# contrasted with MultiplayerSandbox's peer-symmetric deterministic lockstep —
# see authority.hpp's header comment for why this genre needs the other model.
#
# Three binaries share authority.hpp/.cpp + protocol.hpp + sim_common.hpp:
#   - HideAndSeekServer: HideAndSeekAuthority in a standalone process, no
#     attached player.
#   - HideAndSeekListenServer: the same HideAndSeekAuthority embedded inside
#     one player's own windowed client process.
#   - HideAndSeekClient: connects to either kind of authority identically —
#     the protocol doesn't distinguish deployment shape.

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT EMSCRIPTEN)
    # ── HideAndSeekServer: HideAndSeekAuthority, no attached player ──────────
    # No AtmosphericEngine dependency at all — no GameObject/Scene/rendering,
    # just UdpSocket (Engine/src/udp_socket.cpp, compiled directly into this
    # executable rather than pulling in the AtmosphericEngine target — it has
    # zero third-party deps of its own) and spdlog for logging. Not an
    # Application: this has no window/GameObject needs, same reasoning as
    # UdpRelay staying a plain class (see Engine/include/Atmospheric/udp_relay.hpp).
    add_executable(HideAndSeekServer
        server_main.cpp
        authority.cpp
        ${CMAKE_SOURCE_DIR}/Engine/src/udp_socket.cpp
    )
    set_target_properties(HideAndSeekServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HideAndSeekServer
    )
    target_include_directories(HideAndSeekServer PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
        # udp_socket.cpp uses a quoted #include "udp_socket.hpp" (matching every
        # other Engine/src/*.cpp), which needs this directory directly on the
        # include path — same reason AtmosphericEngine's own target has it.
        ${CMAKE_SOURCE_DIR}/Engine/include/Atmospheric
    )
    target_link_libraries(HideAndSeekServer PRIVATE spdlog::spdlog)
    if(WIN32)
        target_link_libraries(HideAndSeekServer PRIVATE ws2_32)
    endif()

    # ── HideAndSeekClient: windowed renderer + input + ClientNet ─────────────
    find_package(glad CONFIG REQUIRED)

    # authority.cpp is compiled in for the --local single-process test mode
    # (an embedded HideAndSeekAuthority pumped over loopback).
    add_executable(HideAndSeekClient main.cpp client_net.cpp authority.cpp)
    set_target_properties(HideAndSeekClient PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HideAndSeekClient
    )
    target_include_directories(HideAndSeekClient PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
    )
    target_link_libraries(HideAndSeekClient PRIVATE
        AtmosphericEngine
        glm::glm
        fmt::fmt
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )
    if(WIN32)
        target_link_libraries(HideAndSeekClient PRIVATE ws2_32)
    endif()

    ae_copy_engine_assets(HideAndSeekClient)
    ae_copy_game_assets(HideAndSeekClient "${CMAKE_CURRENT_SOURCE_DIR}/assets")

    # ── HideAndSeekListenServer: HideAndSeekAuthority embedded in a client ───
    # Needs the full engine (it's a windowed Application, same as
    # HideAndSeekClient) plus authority.cpp/client_net.cpp for the embedded
    # authority + the host's own loopback connection to it.
    add_executable(HideAndSeekListenServer listen_server_main.cpp authority.cpp client_net.cpp)
    set_target_properties(HideAndSeekListenServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HideAndSeekListenServer
    )
    target_include_directories(HideAndSeekListenServer PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
    )
    target_link_libraries(HideAndSeekListenServer PRIVATE
        AtmosphericEngine
        glm::glm
        fmt::fmt
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )
    if(WIN32)
        target_link_libraries(HideAndSeekListenServer PRIVATE ws2_32)
    endif()

    ae_copy_engine_assets(HideAndSeekListenServer)
    ae_copy_game_assets(HideAndSeekListenServer "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
