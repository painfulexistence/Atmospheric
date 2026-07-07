# Deathmatch: 2-player, server-authoritative, Quake-style arena shooter, built
# to demonstrate lag compensation and the favor-the-shooter vs favor-the-target
# tradeoff — the FPS-lineage netcode HideAndSeek's proximity-tag mechanic never
# needed. See authority.hpp for the design.
#
# Two binaries share authority/protocol/sim + client_net:
#   - DeathmatchServer: DeathmatchAuthority in a standalone process.
#   - DeathmatchClient: windowed renderer + input + ClientNet.

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT EMSCRIPTEN)
    # ── DeathmatchServer: DeathmatchAuthority, no attached player ────────────
    # No AtmosphericEngine dependency — no GameObject/Scene/rendering, just
    # UdpSocket (compiled directly in, zero third-party deps) and spdlog. Same
    # plain-loop reasoning as HideAndSeekServer / UdpRelay.
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

    # ── DeathmatchClient: windowed renderer + input + ClientNet ─────────────
    find_package(glad CONFIG REQUIRED)

    add_executable(DeathmatchClient main.cpp client_net.cpp)
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
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )
    if(WIN32)
        target_link_libraries(DeathmatchClient PRIVATE ws2_32)
    endif()

    ae_copy_engine_assets(DeathmatchClient)
    ae_copy_game_assets(DeathmatchClient "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
