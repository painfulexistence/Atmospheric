#pragma once

// Engine configuration.
//
// Convention:
//   • Values (dimensions, counts, indices) are `inline constexpr` — typed and
//     scoped, no macro substitution surprises.
//   • Feature toggles stay `#define` — several are consumed by `#if` in
//     platform/threading paths, where the preprocessor can't see constexpr.

// ── Window / framebuffer ────────────────────────────────────────────────────
inline constexpr const char* INIT_SCREEN_TITLE = "Atmospheric";
inline constexpr int INIT_SCREEN_WIDTH = 1120;// unit
inline constexpr int INIT_SCREEN_HEIGHT = 840;// unit
inline constexpr int INIT_FRAMEBUFFER_WIDTH = 1120;// px
inline constexpr int INIT_FRAMEBUFFER_HEIGHT = 840;// px

// ── Feature toggles (macros: some feed #if) ─────────────────────────────────
#define SINGLE_THREAD 1
#define RUNTIME_LOG_ON 1
#define SHOW_PROCESS_COST 0
#define SHOW_RENDER_AND_DRAW_COST 0
#define SHOW_SYNC_COST 0
#define SHOW_VSYNC_COST 0
#define VSYNC_ON 1
#define FRUSTUM_CULLING_ON 1
#define MSAA_ON 1

// ── Graphics ────────────────────────────────────────────────────────────────
inline constexpr int SHADOW_W = 1024;
inline constexpr int SHADOW_H = 1024;
inline constexpr int SHADOW_CASCADES = 3;
inline constexpr int MAX_UNI_LIGHTS = 1;
// NOTE: at least one omni shadow map is currently required — there is no way to
// disable shadows yet.
inline constexpr int MAX_OMNI_LIGHTS = 1;
inline constexpr int MSAA_NUM_SAMPLES = 4;

// GL texture-unit layout: shadow maps occupy the low units, scene textures
// follow. (Derived reserve indices — kept for the texture-unit allocator.)
inline constexpr int UNI_SHADOW_MAP_COUNT = MAX_UNI_LIGHTS;
inline constexpr int OMNI_SHADOW_MAP_COUNT = MAX_OMNI_LIGHTS;
inline constexpr int SHADOW_MAP_COUNT = UNI_SHADOW_MAP_COUNT + OMNI_SHADOW_MAP_COUNT;
inline constexpr int DEFAULT_TEXTURE_BASE_INDEX = SHADOW_MAP_COUNT;
inline constexpr int DEFAULT_TEXTURE_COUNT = 0;
inline constexpr int SCENE_TEXTURE_BASE_INDEX = SHADOW_MAP_COUNT + DEFAULT_TEXTURE_COUNT;
