set(GAME_NAME VideoPlayer)
set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# FFmpeg is required for VideoPlayer.
# Configure the top-level build with: -DAE_USE_FFMPEG=ON
# (or pass -DAE_USE_FFMPEG=ON to the AtmosphericEngine target before this).
if(EMSCRIPTEN OR ANDROID OR IOS)
    message(STATUS "Skipping ${GAME_NAME}: not supported on this platform "
                   "(FFmpeg unavailable on WASM / mobile).")
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
    glad::glad
    spdlog::spdlog
)

# Sync engine default assets (shaders, fonts, etc.)
add_custom_command(
    TARGET ${GAME_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Engine/default_assets
        $<TARGET_FILE_DIR:${GAME_TARGET}>/assets
    COMMENT "Syncing default assets for ${GAME_NAME}"
)

# Sync game-specific assets (video files, etc.) if the directory exists.
if(EXISTS ${GAME_ASSETS_DIR})
    add_custom_command(
        TARGET ${GAME_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${GAME_ASSETS_DIR}
            $<TARGET_FILE_DIR:${GAME_TARGET}>/assets
        COMMENT "Syncing game assets for ${GAME_NAME}"
    )
endif()
