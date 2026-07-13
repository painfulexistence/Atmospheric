# USDViewer — USD scene viewer over the unified prefab import line.
# Registered by Examples/CMakeLists.txt when AE_USE_TINYUSDZ is on (default on
# every platform, web included; -DAE_USE_TINYUSDZ=OFF opts out).
set(GAME_NAME USDViewer)
set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)

set(GAME_TARGET ${GAME_NAME})
add_executable(${GAME_TARGET} main.cpp)
set_target_properties(${GAME_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)

target_include_directories(${GAME_TARGET} PUBLIC ${CMAKE_SOURCE_DIR}/Engine/include)
target_link_libraries(${GAME_TARGET} PRIVATE
    AtmosphericEngine
    glm::glm
    fmt::fmt
    BulletSoftBody BulletDynamics BulletCollision LinearMath
)

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly build ──────────────────────────────────────
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Stage assets that must be available synchronously before the link step so
    # --preload-file can bake them into the .data bundle (they land in MEMFS,
    # where fopen() — and thus tinyusdz's LoadUSDFromFile — reads them).
    # cube.usda is the self-contained web demo (Kitchen_set is native-only:
    # ~30MB with hundreds of external references, not a realistic web payload).
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
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/textures
            ${EM_PRELOAD_STAGING_DIR}/textures
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes
            ${EM_PRELOAD_STAGING_DIR}/scenes
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/models
            ${EM_PRELOAD_STAGING_DIR}/models
        COMMENT "Staging Emscripten preload assets (shaders, fonts, ui, textures, scenes, models) for ${GAME_NAME}"
    )
    add_dependencies(${GAME_TARGET} stage_${GAME_NAME}_em_preload)

    target_link_options(${GAME_TARGET} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/ui@assets/ui"
        # Default PBR textures (default_*.ktx2) — LoadDefaultTextures() reads
        # these synchronously when useDefaultTextures=true, so they must be in
        # MEMFS. ae_copy_engine_assets skips Emscripten, hence this preload.
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/textures@assets/textures"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/scenes@assets/scenes"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/models@assets/models"
    )

    # Release builds use the centered custom shell (matches other examples);
    # Debug keeps Emscripten's default shell for easier console/stdout debugging.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${GAME_TARGET} PRIVATE "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    endif()
else()
    # ── Native build ─────────────────────────────────────────────────────────
    if(NOT IOS)
        find_package(spdlog CONFIG REQUIRED)
        find_package(glad CONFIG REQUIRED)
        target_link_libraries(${GAME_TARGET} PRIVATE glad::glad spdlog::spdlog raudio)
    endif()
    target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3)
endif()

ae_copy_engine_assets(${GAME_TARGET})
ae_copy_game_assets(${GAME_TARGET} "${GAME_ASSETS_DIR}")
