set(GAME_NAME MultiplayerSandbox)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(box2d CONFIG REQUIRED)

add_executable(${GAME_NAME}
    main.cpp
    sand_world.cpp
    game_sim.cpp
    net_lockstep.cpp
)
set_target_properties(${GAME_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)

target_include_directories(${GAME_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/include
)

# Base dependencies (available on all platforms)
target_link_libraries(${GAME_NAME} PRIVATE
    AtmosphericEngine   # pulls in lua_static transitively
    glm::glm
    fmt::fmt
    BulletSoftBody
    BulletDynamics
    BulletCollision
    LinearMath
    box2d::box2d
)

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly build (solo mode only: no UDP sockets) ────
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets)

    add_custom_target(stage_${GAME_NAME}_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/shaders
            ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/fonts
            ${EM_PRELOAD_STAGING_DIR}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/ui
            ${EM_PRELOAD_STAGING_DIR}/ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui
            ${EM_PRELOAD_STAGING_DIR}/ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes
            ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMENT "Staging Emscripten preload assets (shaders, fonts, ui, scenes) for ${GAME_NAME}"
    )
    add_dependencies(${GAME_NAME} stage_${GAME_NAME}_em_preload)

    target_link_options(${GAME_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sINVOKE_RUN=0
        -sEXPORTED_FUNCTIONS=["_main","_printMemoryStats"]
        -Wl,--undefined=printMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/ui@assets/ui"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
    )


    # Use custom centered template only for Release builds
    # Keep default Emscripten shell layout in Debug builds for easier console/stdout debugging
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${GAME_NAME} PRIVATE "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    endif()
else()
    # ── Native build ─────────────────────────────────────────────────────────
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)

    target_link_libraries(${GAME_NAME} PRIVATE
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )

    # Winsock2 for the UDP lockstep transport on Windows.
    if(WIN32)
        target_link_libraries(${GAME_NAME} PRIVATE ws2_32)
    endif()

    # ── Relay loopback integration test (runs in CI via ctest) ──────────────
    # One process: UdpRelayServer (engine) + two LockstepNet peers over
    # 127.0.0.1. Exercises the relay handshake, input exchange, and
    # stale-peer rebind without any real network.
    add_executable(RelayLoopbackTest
        relay_loopback_test.cpp
        net_lockstep.cpp
    )
    target_include_directories(RelayLoopbackTest PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
    )
    target_link_libraries(RelayLoopbackTest PRIVATE AtmosphericEngine)
    if(WIN32)
        target_link_libraries(RelayLoopbackTest PRIVATE ws2_32)
    endif()
    add_test(NAME RelayLoopback COMMAND RelayLoopbackTest)
    set_tests_properties(RelayLoopback PROPERTIES TIMEOUT 60)
endif()

# ── Asset sync ──
ae_copy_engine_assets(${GAME_NAME})
ae_copy_game_assets(${GAME_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/assets")
