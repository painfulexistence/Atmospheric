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
            ${CMAKE_CURRENT_SOURCE_DIR}/scenes
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


    # Use custom centered template only for Release builds
    # Keep default Emscripten shell layout in Debug builds for easier console/stdout debugging
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${TARGET_NAME} PRIVATE "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    endif()
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
ae_copy_engine_assets(${TARGET_NAME})
# Ensure scripts/ directory exists for Lua script loading
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets/scripts"
    VERBATIM
)
ae_copy_game_assets(${TARGET_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/assets")
ae_copy_scenes(${TARGET_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/scenes")
ae_convert_textures(${TARGET_NAME}
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures"
    "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets/textures"
)
