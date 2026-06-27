set(GAME_NAME VideoPlayer)
set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# FFmpeg is required for native VideoPlayer.
if(ANDROID OR IOS)
    message(STATUS "Skipping ${GAME_NAME}: not supported on this platform "
                   "(FFmpeg unavailable on mobile).")
    return()
endif()

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

set(GAME_TARGET ${GAME_NAME})
add_executable(${GAME_TARGET} main.cpp)
set_target_properties(${GAME_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
)

target_include_directories(${GAME_TARGET} PUBLIC
    ${CMAKE_SOURCE_DIR}/Engine/include
)

target_link_libraries(${GAME_TARGET} PRIVATE
    AtmosphericEngine
    glm::glm
    fmt::fmt
)

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly build ──────────────────────────────────────
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
        COMMENT "Staging Emscripten preload assets (shaders, fonts) for ${GAME_NAME}"
    )
    add_dependencies(${GAME_TARGET} stage_${GAME_NAME}_em_preload)
    
    target_link_options(${GAME_TARGET} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printMemoryStats"]
        -Wl,--undefined=printMemoryStats
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/shaders@assets/shaders"
        "SHELL:--preload-file ${EM_PRELOAD_STAGING_DIR}/fonts@assets/fonts"
    )
    
    # Use custom centered template only for Release builds
    # Keep default Emscripten shell layout in Debug builds for easier console/stdout debugging
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${GAME_TARGET} PRIVATE "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    endif()
    
else()
    # ── Native build ─────────────────────────────────────────────────────────
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    
    target_link_libraries(${GAME_TARGET} PRIVATE
        glad::glad
        spdlog::spdlog
        raudio
    )
    
    target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3)
endif()

ae_copy_engine_assets(${GAME_TARGET})
ae_copy_game_assets(${GAME_TARGET} "${GAME_ASSETS_DIR}")
ae_copy_scenes(${GAME_TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/scenes")

# Download Sprite Fright (CC-BY, Blender Studio) on first build.
# ~150 MB; skipped on subsequent builds if the file already exists.
# We also run this on Emscripten so the video is hosted on the server and streamed by HTML5 Video.
set(SF_URL "https://upload.wikimedia.org/wikipedia/commons/7/74/Sprite_Fright_-_Open_Movie_by_Blender_Studio.webm")
set(SF_DST "$<TARGET_FILE_DIR:${GAME_TARGET}>/assets/video/sprite_fright.webm")
add_custom_command(
    TARGET ${GAME_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DURL=${SF_URL}
        -DDST=${SF_DST}
        -P ${CMAKE_SOURCE_DIR}/cmake/download_if_missing.cmake
    COMMENT "Fetching Sprite Fright demo video (first build only, ~150 MB)"
)
