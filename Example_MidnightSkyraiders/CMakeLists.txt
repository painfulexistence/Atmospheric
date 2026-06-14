set(GAME_NAME MidnightSkyraiders)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)

add_executable(${GAME_NAME} main.cpp)
set_target_properties(${GAME_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)
target_include_directories(${GAME_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/AtmosphericEngine/include
)
target_link_libraries(${GAME_NAME} PRIVATE
    AtmosphericEngine
    glm::glm
    fmt::fmt
    BulletSoftBody BulletDynamics BulletCollision LinearMath
)

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets)

    add_custom_target(stage_${GAME_NAME}_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets/shaders
            ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets/fonts
            ${EM_PRELOAD_STAGING_DIR}/fonts
        COMMENT "Staging Emscripten preload assets for ${GAME_NAME}"
    )
    add_dependencies(${GAME_NAME} stage_${GAME_NAME}_em_preload)

    target_link_options(${GAME_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printWasmMemoryStats"]
        -Wl,--undefined=printWasmMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
    )
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

# ── Asset sync ───────────────────────────────────────────────────────────────
add_custom_target(sync_default_assets_${GAME_NAME} ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets
    COMMENT "Syncing default assets for ${GAME_NAME}"
)
add_dependencies(sync_default_assets_${GAME_NAME} ${GAME_NAME})

add_custom_target(sync_game_assets_${GAME_NAME} ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets
    COMMENT "Syncing game assets for ${GAME_NAME}"
)
add_dependencies(sync_game_assets_${GAME_NAME} ${GAME_NAME})

ae_auto_convert_textures(${GAME_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/images
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets/images
)
