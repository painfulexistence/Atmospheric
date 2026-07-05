# HiddenTag: server-authoritative 1 Seeker vs 1 Hider hide-and-tag, contrasted
# with MultiplayerSandbox's peer-symmetric deterministic lockstep — see
# server_main.cpp's header comment for why this genre needs the other model.

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT EMSCRIPTEN)
    # ── HiddenTagServer: authoritative dedicated server ──────────────────────
    # No AtmosphericEngine dependency at all — no GameObject/Scene/rendering,
    # just its own UDP socket (own platform shim, mirroring
    # MultiplayerSandbox/net_lockstep.cpp's) and spdlog for logging. Not an
    # Application: this has no window/GameObject needs, same reasoning as
    # UdpRelay staying a plain class (see Engine/include/Atmospheric/udp_relay.hpp).
    add_executable(HiddenTagServer server_main.cpp)
    set_target_properties(HiddenTagServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/HiddenTag
    )
    target_link_libraries(HiddenTagServer PRIVATE spdlog::spdlog)
    if(WIN32)
        target_link_libraries(HiddenTagServer PRIVATE ws2_32)
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
endif()
