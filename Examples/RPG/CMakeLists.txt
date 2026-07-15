set(GAME_NAME RPG)
set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)

add_executable(${GAME_NAME}
    main.cpp
)
set_target_properties(${GAME_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)

target_include_directories(${GAME_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${GAME_NAME} PRIVATE
    AtmosphericEngine
    glm::glm
    fmt::fmt
    BulletSoftBody
    BulletDynamics
    BulletCollision
    LinearMath
)

if(EMSCRIPTEN)
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
        COMMAND ${CMAKE_COMMAND}
            -Dsrc=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
            -Ddst=${EM_PRELOAD_STAGING_DIR}/fonts
            -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/ui
            ${EM_PRELOAD_STAGING_DIR}/ui
        # Merge this game's own UI documents (hud/battle/dialogue .rml + .rcss)
        # into the staged ui dir so they are preloaded alongside the engine's.
        COMMAND ${CMAKE_COMMAND}
            -Dsrc=${CMAKE_CURRENT_SOURCE_DIR}/assets/ui
            -Ddst=${EM_PRELOAD_STAGING_DIR}/ui
            -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake
        COMMENT "Staging Emscripten preload assets for ${GAME_NAME}"
    )
    add_dependencies(${GAME_NAME} stage_${GAME_NAME}_em_preload)

    set(RPG_EM_LINK_OPTIONS
        -sALLOW_MEMORY_GROWTH=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main"]
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/ui@assets/ui"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND RPG_EM_LINK_OPTIONS -g -sASSERTIONS=2)
    else()
        list(APPEND RPG_EM_LINK_OPTIONS -O3 -sASSERTIONS=1)
    endif()
    target_link_options(${GAME_NAME} PRIVATE ${RPG_EM_LINK_OPTIONS})

    # Use custom centered template only for Release builds
    # Keep default Emscripten shell layout in Debug builds for easier console/stdout debugging
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${GAME_NAME} PRIVATE "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    endif()
else()
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    target_link_libraries(${GAME_NAME} PRIVATE glad::glad spdlog::spdlog raudio SDL3::SDL3)
endif()

ae_copy_engine_assets(${GAME_NAME})
ae_copy_game_assets(${GAME_NAME} "${GAME_ASSETS_DIR}")
