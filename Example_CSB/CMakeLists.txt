set(GAME_NAME CSBDemo)
set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(box2d CONFIG REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)

add_executable(${GAME_NAME}
    main.cpp
)
set_target_properties(${GAME_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)

target_include_directories(${GAME_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/AtmosphericEngine/include
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
    flatbuffers::flatbuffers
)

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly build ──────────────────────────────────────
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Stage shaders, scenes, and fonts BEFORE the link step
    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets)

    add_custom_target(stage_${GAME_NAME}_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets/shaders
            ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes
            ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMAND ${CMAKE_COMMAND}
            -Dsrc=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
            -Ddst=${EM_PRELOAD_STAGING_DIR}/fonts
            -P ${CMAKE_SOURCE_DIR}/cmake/copy_dir_if_exists.cmake
        COMMENT "Staging Emscripten preload assets (shaders, scenes, fonts) for ${GAME_NAME}"
    )
    add_dependencies(${GAME_NAME} stage_${GAME_NAME}_em_preload)

    set(_em_link_opts
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printWasmMemoryStats"]
        -Wl,--undefined=printWasmMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
    )
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)
        list(APPEND _em_link_opts
            "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
        )
    endif()
    target_link_options(${GAME_NAME} PRIVATE ${_em_link_opts})

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
endif()

# ── Asset sync for desktop ──
if(NOT EMSCRIPTEN)
    add_custom_command(
        TARGET ${GAME_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets
        $<TARGET_FILE_DIR:${GAME_NAME}>/assets
    )

    if(EXISTS ${GAME_ASSETS_DIR})
        add_custom_command(
            TARGET ${GAME_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${GAME_ASSETS_DIR}
            $<TARGET_FILE_DIR:${GAME_NAME}>/assets
        )
    endif()
endif()
