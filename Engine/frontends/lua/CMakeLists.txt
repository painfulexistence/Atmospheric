# ── AtmosLua — Löve2D-style Lua game runtime ─────────────────────────────────
# A first-class engine frontend (sibling to the AtmosWasm `atmospheric` target).
# Builds the reusable Lua runtime executable; a game is just an
# assets/scripts/main.lua that this binary loads at startup.
#
# AtmosphericEngine PUBLICLY propagates glm / fmt / Bullet / lua_static / sol2 /
# box2d / RmlUi / flatbuffers and the platform libs (SDL3 / glad / spdlog /
# raudio), so the runtime only needs to link the engine itself.

set(_atmoslua_sources
    runtime.cpp
    lua_application.cpp
    scriptable_component.cpp
    bindings/bind_core.cpp
    bindings/bind_input.cpp
    bindings/bind_world.cpp
    bindings/bind_graphics.cpp
    bindings/bind_physics.cpp
    bindings/bind_audio.cpp
)

add_executable(AtmosLua ${_atmoslua_sources})

target_include_directories(AtmosLua PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../external/stb
)

target_link_libraries(AtmosLua PRIVATE AtmosphericEngine)

set_target_properties(AtmosLua PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/AtmosLua
)

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Bake only the engine's game-independent defaults (shaders / fonts / ui).
    # Game scripts + textures are fetched at runtime by runtime.cpp
    # (FileSystem::Prefetch), so a Lua game = this binary + its assets served
    # alongside — mirroring how the `atmospheric` target ships default_assets.
    set(_atmoslua_preload ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/AtmosLua/assets)
    add_custom_target(stage_atmoslua_em_preload
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_atmoslua_preload}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/shaders ${_atmoslua_preload}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/fonts ${_atmoslua_preload}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets/ui ${_atmoslua_preload}/ui
        COMMENT "Staging AtmosLua Emscripten preload assets (engine defaults)"
    )
    add_dependencies(AtmosLua stage_atmoslua_em_preload)

    target_link_options(AtmosLua PRIVATE
        -sWASM=1 -O3 -sUSE_GLFW=3
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
        -sFETCH=1 -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sEXPORTED_FUNCTIONS=["_main","_printMemoryStats"]
        -Wl,--undefined=printMemoryStats
        "SHELL:--preload-file ${_atmoslua_preload}/shaders@assets/shaders"
        "SHELL:--preload-file ${_atmoslua_preload}/fonts@assets/fonts"
        "SHELL:--preload-file ${_atmoslua_preload}/ui@assets/ui"
    )
endif()
