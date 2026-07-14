# Examples are ordered as living documentation: a progressive tour of engine
# features — 2D first, then 3D, then networking, with the Lua scripting
# frontend last. (Getting-started / minimal 2D sprite lives in the separate
# Atmospheric-Starter repo.)

# ── 2D ───────────────────────────────────────────────────────────────────────
add_subdirectory(Animation)          # sprite-sheet 2D animation (FlipbookComponent)
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

# ── Networking ───────────────────────────────────────────────────────────────
add_subdirectory(MultiplayerSandbox) # deterministic lockstep netplay
if(NOT EMSCRIPTEN)
    add_subdirectory(RelayServer)    # headless UDP relay (server deployment shape)
    add_subdirectory(HideAndSeek)    # server-authoritative asymmetric PvP netcode
endif()
# Deathmatch's client also builds for web as a single-player (--local) demo: the
# embedded authority is reached over an in-process LoopbackDatagramSocket, so no
# UDP/WebTransport is needed. Its dedicated server stays native-only (guarded
# inside). Lag compensation + prediction/reconciliation FPS netcode.
add_subdirectory(Deathmatch)

# ── Scripting ────────────────────────────────────────────────────────────────
add_subdirectory(LuaScripting)       # Löve2D-style Lua scripting frontend

#add_subdirectory(MazeFPS)
