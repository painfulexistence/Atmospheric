# LuaScripting — a Lua game (content only).
#
# The Lua runtime itself is the AtmosLua frontend, built once as part of the
# engine (Engine/frontends/lua). This example provides nothing but its Lua
# scripts + scenes: it depends on AtmosLua and stages the runtime binary next
# to its own assets so the pair is a self-contained, runnable demo under a
# LuaScripting/ directory.
#
# The staged binary keeps its honest `AtmosLua` name — the tooling does not
# rely on it: smoke.sh runs the unique executable it finds in the folder, and
# deployWWW.sh serves the folder by directory name (renaming the single .html
# to index.html). Any future Lua example follows the same pattern for free.

if(NOT TARGET AtmosLua)
    message(STATUS "Skipping LuaScripting: AtmosLua frontend is not built "
                   "(AE_BUILD_LUA_FRONTEND is OFF or unsupported on this platform).")
    return()
endif()

set(_lua_out ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/LuaScripting)
set(_lua_assets ${CMAKE_CURRENT_SOURCE_DIR}/assets)

if(EMSCRIPTEN)
    add_custom_target(LuaScripting ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_lua_out}
        # Runtime artifacts (AtmosLua.html/.js/.wasm/.data) reference each other
        # by name, so copy them in verbatim.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/AtmosLua ${_lua_out}
        # Game scripts are fetched at runtime → serve them next to the binary.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${_lua_assets} ${_lua_out}/assets
        COMMENT "Staging AtmosLua + LuaScripting game assets (web)"
        VERBATIM
    )
else()
    add_custom_target(LuaScripting ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_lua_out}
        # Copy the runtime binary in as-is (keeps the name AtmosLua).
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:AtmosLua> ${_lua_out}/
        # Engine defaults (shaders/fonts/ui/...) the runtime loads from ./assets.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Engine/default_assets ${_lua_out}/assets
        # This game's scripts + scenes (overlaid on top of the engine defaults).
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${_lua_assets} ${_lua_out}/assets
        COMMENT "Staging AtmosLua runtime + LuaScripting game assets"
        VERBATIM
    )
endif()

add_dependencies(LuaScripting AtmosLua)
