set(TARGET_NAME AtmosLua)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
# lua_static and sol2::sol2 are PUBLIC deps of AtmosphericEngine — no find_package needed

# Source files
set(LUA_FRONTEND_SOURCES
    main.cpp
    lua_application.cpp
    scriptable_component.cpp
    bindings/bind_core.cpp
    bindings/bind_input.cpp
    bindings/bind_world.cpp
    bindings/bind_graphics.cpp
    bindings/bind_physics.cpp
    bindings/bind_audio.cpp
)

add_executable(${TARGET_NAME} ${LUA_FRONTEND_SOURCES})

set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AtmosphericEngine/include
        ${CMAKE_SOURCE_DIR}/AtmosphericEngine/external/stb
        # LUA_INCLUDE_DIR removed: Lua headers propagate from AtmosphericEngine → lua_static
)

# Base link libraries (all platforms)
target_link_libraries(${TARGET_NAME}
    PRIVATE
        AtmosphericEngine   # pulls in lua_static + sol2::sol2 transitively
        glm::glm
        fmt::fmt
        BulletSoftBody
        BulletDynamics
        BulletCollision
        LinearMath
)

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly build ──────────────────────────────────────
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Stage shaders BEFORE the link step (--preload-file requires the source
    # path to exist at link time; POST_BUILD is too late).
    # Textures and scripts are loaded dynamically via FileSystem::Prefetch.
    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/assets)

    add_custom_target(stage_${TARGET_NAME}_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets/shaders
            ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes
            ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMENT "Staging Emscripten preload assets (shaders, scenes) for ${TARGET_NAME}"
    )
    add_dependencies(${TARGET_NAME} stage_${TARGET_NAME}_em_preload)

    # -sFETCH=1               : emscripten_fetch API (used by FileSystem on web)
    # -sFORCE_FILESYSTEM=1    : ensure FS.writeFile / FS.mkdir are linked in
    #                           even when Emscripten's dead-code elimination
    #                           might otherwise strip them (needed by FileSystem
    #                           MEMFS writes for .lua / .json / .glsl files)
    # -sALLOW_MEMORY_GROWTH=1 : let the WASM heap grow; prevents OOM when
    #                           multiple KTX2 textures are transcoded in sequence
    target_link_options(${TARGET_NAME} PRIVATE
        -sWASM=1 -O3 -sUSE_GLFW=3
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
        -sFETCH=1 -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printWasmMemoryStats"]
        -Wl,--undefined=printWasmMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
    )

else()
    # ── Native build ─────────────────────────────────────────────────────────
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            glad::glad
            spdlog::spdlog
    )
    target_link_libraries(${TARGET_NAME} PRIVATE raudio SDL3::SDL3)
endif()

# ── Asset sync ───────────────────────────────────────────────────────────────
# ALL targets run on every cmake --build invocation, not only when the binary
# relinks.  This means shader edits and new textures are reflected immediately.
add_custom_target(sync_default_assets_${TARGET_NAME} ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/AtmosphericEngine/default_assets
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/assets/scripts
    COMMENT "Syncing default assets for ${TARGET_NAME}"
)
add_dependencies(sync_default_assets_${TARGET_NAME} ${TARGET_NAME})

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    add_custom_target(sync_game_assets_${TARGET_NAME} ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/assets
        COMMENT "Syncing game assets for ${TARGET_NAME}"
    )
    add_dependencies(sync_game_assets_${TARGET_NAME} ${TARGET_NAME})
    
    # Auto-convert PNG/JPG textures to KTX2 for Emscripten builds
    ae_auto_convert_textures(${TARGET_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}/assets/textures
    )
endif()
