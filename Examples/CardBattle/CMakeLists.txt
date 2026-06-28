set(GAME_NAME CardBattle)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)

add_executable(${GAME_NAME} main.cpp)
set_target_properties(${GAME_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)
target_include_directories(${GAME_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(${GAME_NAME} PRIVATE
    AtmosphericEngine
    glm::glm
    fmt::fmt
    BulletSoftBody BulletDynamics BulletCollision LinearMath
)

set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

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
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${GAME_ASSETS_DIR}/scenes
            ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMENT "Staging Emscripten preload assets (shaders, fonts, scenes) for ${GAME_NAME}"
    )
    add_dependencies(${GAME_NAME} stage_${GAME_NAME}_em_preload)

    target_link_options(${GAME_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printMemoryStats"]
        -Wl,--undefined=printMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
    )

    # Use custom centered template only for Release builds
    # Keep default Emscripten shell layout in Debug builds for easier console/stdout debugging
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${GAME_NAME} PRIVATE "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    endif()
else()
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    target_link_libraries(${GAME_NAME} PRIVATE
        glad::glad
        spdlog::spdlog
        raudio
        SDL3::SDL3
    )
endif()

# ── Asset sync ──
ae_copy_engine_assets(${GAME_NAME})
ae_copy_game_assets(${GAME_NAME} "${GAME_ASSETS_DIR}")
