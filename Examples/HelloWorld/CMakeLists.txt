set(GAME_NAME HelloWorld)
set(GAME_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
# lua_static is a PUBLIC dep of AtmosphericEngine — no find_package needed

if(ANDROID)
    set(GAME_TARGET main)
    add_library(${GAME_TARGET} SHARED main.cpp)
elseif(IOS)
    set(GAME_TARGET main)
    add_executable(${GAME_TARGET} MACOSX_BUNDLE main.cpp)
else()
    set(GAME_TARGET ${GAME_NAME})
    add_executable(${GAME_TARGET} main.cpp)
    set_target_properties(${GAME_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}
    )
endif()

target_include_directories(${GAME_TARGET} PUBLIC ${CMAKE_SOURCE_DIR}/Engine/include)

# Base dependencies (available on all platforms)
target_link_libraries(${GAME_TARGET} PRIVATE
    AtmosphericEngine   # pulls in lua_static transitively
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

    # ── Stage shader and font assets BEFORE the link step ───────────────────
    # emscripten --preload-file requires the source path to exist at link time.
    # POST_BUILD is too late; we use a PRE_LINK target instead.
    #
    # Textures are intentionally NOT included here — they are loaded at runtime
    # via FileSystem::Prefetch (emscripten_fetch + IndexedDB), which avoids
    # bloating the .data bundle and reduces peak WASM heap usage.
    set(EM_PRELOAD_STAGING_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GAME_NAME}/assets)

    add_custom_target(stage_${GAME_NAME}_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EM_PRELOAD_STAGING_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/shaders
            ${EM_PRELOAD_STAGING_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/fonts
            ${EM_PRELOAD_STAGING_DIR}/fonts
        COMMAND ${CMAKE_COMMAND}
            -Dsrc=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
            -Ddst=${EM_PRELOAD_STAGING_DIR}/fonts
            -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake
        COMMAND ${CMAKE_COMMAND}
            -Dsrc=${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes
            -Ddst=${EM_PRELOAD_STAGING_DIR}/scenes
            -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake
        COMMENT "Staging Emscripten preload assets (shaders, fonts, scenes) for ${GAME_NAME}"
    )
    add_dependencies(${GAME_TARGET} stage_${GAME_NAME}_em_preload)

    # ── Emscripten link options ──────────────────────────────────────────────
    # --preload-file  shaders + fonts only (small, needed synchronously).
    # Textures: fetched asynchronously via FileSystem::Prefetch in main.cpp.
    # -sFETCH=1       : inherited from AtmosphericEngine but repeated here for
    #                   the executable's link step (FileSystem web implementation).
    # -sALLOW_MEMORY_GROWTH=1 : allows WASM heap to grow dynamically; avoids
    #                   OOM when loading multiple large textures in sequence.
    target_link_options(${GAME_TARGET} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
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

# ── Asset sync ───────────────────────────────────────────────────────────────
ae_copy_engine_assets(${GAME_TARGET})
ae_copy_game_assets(${GAME_TARGET} "${GAME_ASSETS_DIR}")
ae_convert_textures(${GAME_TARGET}
    "${GAME_ASSETS_DIR}/textures"
    "$<TARGET_FILE_DIR:${GAME_TARGET}>/assets/textures"
)

if(ANDROID)
    # Automatically copy SDL3 Java sources from the fetched dependency to Gradle source sets
    include(FetchContent)
    FetchContent_GetProperties(SDL3)
    set(SDL3_JAVA_SRC_DIR "")
    if(sdl3_SOURCE_DIR)
        set(SDL3_JAVA_SRC_DIR "${sdl3_SOURCE_DIR}/android-project/app/src/main/java/org")
    elseif(SDL3_SOURCE_DIR)
        set(SDL3_JAVA_SRC_DIR "${SDL3_SOURCE_DIR}/android-project/app/src/main/java/org")
    endif()

    if(SDL3_JAVA_SRC_DIR AND EXISTS "${SDL3_JAVA_SRC_DIR}")
        message(STATUS "Android CMake: Copying SDL3 Java sources from ${SDL3_JAVA_SRC_DIR}")
        file(COPY "${SDL3_JAVA_SRC_DIR}"
             DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/platforms/android/app/src/main/java")
    else()
        message(WARNING "Android CMake: SDL3 Java sources not found at '${SDL3_JAVA_SRC_DIR}'!")
    endif()
endif()

