# USDViewer — USD scene viewer over the unified prefab import line.
# Only registered by Examples/CMakeLists.txt when AE_USE_TINYUSDZ=ON (native).
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
if(NOT IOS)
    find_package(spdlog CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    target_link_libraries(${GAME_TARGET} PRIVATE glad::glad spdlog::spdlog raudio)
endif()
target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3)

ae_copy_engine_assets(${GAME_TARGET})
ae_copy_game_assets(${GAME_TARGET} "${GAME_ASSETS_DIR}")
