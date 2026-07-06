# HiddenTag: server-authoritative 1 Seeker vs 1 Hider hide-and-tag, contrasted
# with MultiplayerSandbox's peer-symmetric deterministic lockstep — see
# authority.hpp's header comment for why this genre needs the other model.
#
# Three binaries share authority.hpp/.cpp + protocol.hpp + sim_common.hpp:
#   - HiddenTagDedicatedServer: HiddenTagAuthority in a standalone process,
#     no attached player.
#   - HiddenTagListenServer: the same HiddenTagAuthority embedded inside one
#     player's own windowed client process.
#   - HiddenTagClient: connects to either kind of authority identically —
#     the protocol doesn't distinguish deployment shape.

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT EMSCRIPTEN)
    # ── HiddenTagDedicatedServer: HiddenTagAuthority, no attached player ─────
    # No AtmosphericEngine dependency at all — no GameObject/Scene/rendering,
    # just UdpSocket (Engine/src/udp_socket.cpp, compiled directly into this
    # executable rather than pulling in the AtmosphericEngine target — it has
    # zero third-party deps of its own) and spdlog for logging. Not an
    # Application: this has no window/GameObject needs, same reasoning as
    # UdpRelay staying a plain class (see Engine/include/Atmospheric/udp_relay.hpp).
    add_executable(HiddenTagDedicatedServer
        dedicated_server_main.cpp
        authority.cpp
        ${CMAKE_SOURCE_DIR}/Engine/src/udp_socket.cpp
    )
    set_target_properties(HiddenTagDedicatedServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HiddenTagDedicatedServer
    )
    target_include_directories(HiddenTagDedicatedServer PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
    )
    target_link_libraries(HiddenTagDedicatedServer PRIVATE spdlog::spdlog)
    if(WIN32)
        target_link_libraries(HiddenTagDedicatedServer PRIVATE ws2_32)
    endif()

    # ── HiddenTagClient: windowed renderer + input + ClientNet ───────────────
    find_package(glad CONFIG REQUIRED)

    add_executable(HiddenTagClient main.cpp client_net.cpp)
    set_target_properties(HiddenTagClient PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HiddenTagClient
    )
    target_include_directories(HiddenTagClient PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
    )
    target_link_libraries(HiddenTagClient PRIVATE
        AtmosphericEngine
        glm::glm
        fmt::fmt
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )
    if(WIN32)
        target_link_libraries(HiddenTagClient PRIVATE ws2_32)
    endif()

    ae_copy_engine_assets(HiddenTagClient)
    ae_copy_game_assets(HiddenTagClient "${CMAKE_CURRENT_SOURCE_DIR}/assets")

    # ── HiddenTagListenServer: HiddenTagAuthority embedded in a client ───────
    # Needs the full engine (it's a windowed Application, same as
    # HiddenTagClient) plus authority.cpp/client_net.cpp for the embedded
    # authority + the host's own loopback connection to it.
    add_executable(HiddenTagListenServer listen_server_main.cpp authority.cpp client_net.cpp)
    set_target_properties(HiddenTagListenServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HiddenTagListenServer
    )
    target_include_directories(HiddenTagListenServer PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
    )
    target_link_libraries(HiddenTagListenServer PRIVATE
        AtmosphericEngine
        glm::glm
        fmt::fmt
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )
    if(WIN32)
        target_link_libraries(HiddenTagListenServer PRIVATE ws2_32)
    endif()

    ae_copy_engine_assets(HiddenTagListenServer)
    ae_copy_game_assets(HiddenTagListenServer "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
