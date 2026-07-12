# Examples are ordered as living documentation: a progressive tour of engine
# features — 2D first, then 3D, then networking, with the Lua scripting
# frontend last. (Getting-started / minimal 2D sprite lives in the separate
# Atmospheric-Starter repo.)

# ── 2D ───────────────────────────────────────────────────────────────────────
add_subdirectory(Animation)          # sprite-sheet 2D animation (Animator2D)
add_subdirectory(MidnightSkyraiders) # 2D sprite gameplay
add_subdirectory(CardBattle)         # immediate-mode 2D primitives, asset-free
add_subdirectory(RPG)                # 2D tilemap + 2D lighting + animation
add_subdirectory(Physics2D)          # Box2D 2D physics
add_subdirectory(VideoPlayer)        # FFmpeg decode → texture

# ── 3D ───────────────────────────────────────────────────────────────────────
add_subdirectory(3DBasics)           # scene JSON + components, PBR, 2D+3D basics
add_subdirectory(VoxelWorld)         # voxel streaming, fly camera, bloom
add_subdirectory(MicroVoxel)         # raymarched micro voxels (DDA over 3D texture)
add_subdirectory(Terrain)            # procedural / heightmap terrain, RmlUi HUD
add_subdirectory(TerrainStreaming)   # 10km x 10km streamed open-world terrain (TerrainStreamer)
if(AE_USE_TINYUSDZ)
    # On by default on every platform, web included (TinyUSDZ builds to WASM).
    add_subdirectory(USDViewer)      # USD scene viewer via the prefab import line
endif()

# ── Networking ───────────────────────────────────────────────────────────────
add_subdirectory(MultiplayerSandbox) # deterministic lockstep netplay
if(NOT EMSCRIPTEN)
    add_subdirectory(RelayServer)    # headless UDP relay (server deployment shape)
    add_subdirectory(HideAndSeek)    # server-authoritative asymmetric PvP netcode
    add_subdirectory(Deathmatch)     # server-authoritative FPS: lag compensation + prediction/reconciliation
endif()

# ── Scripting ────────────────────────────────────────────────────────────────
add_subdirectory(LuaScripting)       # Löve2D-style Lua scripting frontend

#add_subdirectory(MazeFPS)
