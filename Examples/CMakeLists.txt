# Examples are ordered as living documentation: a progressive tour of engine
# features — 2D first, then 3D, then subsystems, then networking, with the Lua
# scripting frontend last. (Getting-started / minimal 2D sprite lives in the
# separate Atmospheric-Starter repo.)

# ── 2D: rendering → physics ──────────────────────────────────────────────────
add_subdirectory(Animation)          # sprite-sheet 2D animation (Animator2D)
add_subdirectory(CardBattle)         # immediate-mode 2D primitives, asset-free
add_subdirectory(MidnightSkyraiders) # 2D sprite gameplay
add_subdirectory(RPG)                # 2D tilemap + 2D lighting + animation
add_subdirectory(Physics2D)          # Box2D 2D physics

# ── 3D ───────────────────────────────────────────────────────────────────────
add_subdirectory(3DBasics)           # scene JSON + components, PBR, 2D+3D basics
add_subdirectory(VoxelWorld)         # voxel streaming, fly camera, bloom
add_subdirectory(Terrain)            # procedural / heightmap terrain, RmlUi HUD

# ── Subsystems ───────────────────────────────────────────────────────────────
add_subdirectory(VideoPlayer)        # FFmpeg decode → texture

# ── Networking ───────────────────────────────────────────────────────────────
add_subdirectory(MultiplayerSandbox) # deterministic lockstep netplay

# ── Scripting frontend ───────────────────────────────────────────────────────
add_subdirectory(LuaScripting)       # Löve2D-style Lua runtime (AtmosLua)

#add_subdirectory(MazeFPS)
