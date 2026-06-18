set(TARGET_NAME AtmosLua)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)

set(LUA_FRONTEND_DIR ${CMAKE_SOURCE_DIR}/Engine/frontends/lua)

set(SOURCES
    ${LUA_FRONTEND_DIR}/runtime.cpp
    ${LUA_FRONTEND_DIR}/lua_application.cpp
    ${LUA_FRONTEND_DIR}/scriptable_component.cpp
    ${LUA_FRONTEND_DIR}/bindings/bind_core.cpp
    ${LUA_FRONTEND_DIR}/bindings/bind_input.cpp
    ${LUA_FRONTEND_DIR}/bindings/bind_world.cpp
    ${LUA_FRONTEND_DIR}/bindings/bind_graphics.cpp
    ${LUA_FRONTEND_DIR}/bindings/bind_physics.cpp
    ${LUA_FRONTEND_DIR}/bindings/bind_audio.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/include
        ${CMAKE_SOURCE_DIR}/Engine/external/stb
        ${LUA_FRONTEND_DIR}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
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

    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/assets)

    add_custom_target(stage_${TARGET_NAME}_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/shaders
            ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/fonts
            ${EM_PRELOAD_STAGING_DIR}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes
            ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMENT "Staging Emscripten preload assets (shaders, scenes) for ${TARGET_NAME}"
    )
    add_dependencies(${TARGET_NAME} stage_${TARGET_NAME}_em_preload)

    target_link_options(${TARGET_NAME} PRIVATE
        -sWASM=1 -O3 -sUSE_GLFW=3
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
        -sFETCH=1 -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printMemoryStats"]
        -Wl,--undefined=printMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
    )

else()
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            glad::glad
            spdlog::spdlog
            raudio
            SDL3::SDL3
    )
endif()

# ── Asset sync ───────────────────────────────────────────────────────────────
add_custom_command(
    TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Engine/default_assets
        $<TARGET_FILE_DIR:${TARGET_NAME}>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:${TARGET_NAME}>/assets/scripts
    COMMENT "Syncing default assets for ${TARGET_NAME}"
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:${TARGET_NAME}>/assets
        COMMENT "Syncing game assets for ${TARGET_NAME}"
    )

    ae_auto_convert_textures(${TARGET_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
        $<TARGET_FILE_DIR:${TARGET_NAME}>/assets/textures
    )
endif()
