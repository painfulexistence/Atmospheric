# Terrain Streaming — AAA Open-World Roadmap

Goal: Ghost-of-Tsushima-class load speed and Breath-of-the-Wild-class
seamlessness / view distance for a 10km × 10km (and larger) terrain, with
Gaea/WorldCreator-grade source data and full splat texturing — no
simplifications baked into the architecture.

This document explains what shipped in **Phase 1** (`TerrainStreamer`), why it
is shaped the way it is, and the phased path to the full AAA feature set
(HLOD, texture streaming, virtual texturing, GPU-driven traversal).

---

## Phase 1 (implemented): tiled ring-LOD streaming

`Engine/include/Atmospheric/terrain_streamer.hpp` /
`Engine/src/terrain_streamer.cpp`, demo in `Examples/TerrainStreaming`.

### Core ideas

**Whole world always resident, at *some* LOD.** The world is split into
square tiles (default 512m over a 10,240m world → 20×20 = 400 tiles). Every
tile always has a mesh + heightmap on the GPU; only the *resolution* changes
with camera distance. This is the BotW property: the horizon never pops in,
there is no fog wall hiding a loading boundary, and view distance equals
world size. It is affordable because a coarse tile is tiny (a 67² R16
heightmap is ~9KB; the whole 400-tile coarse world is a few MB).

**Ghost-of-Tsushima-style boot: coarse-first prewarm.** `Init()` generates
*all* tiles at the coarsest LOD in parallel on the `JobSystem` and uploads
them synchronously — the full 10km horizon is visible on frame one, typically
in well under a second. Detail then refines in the background. GoT does the
same thing conceptually: load a low-res whole-island representation first,
then stream detail near the player; the player can start moving immediately.

**Concentric LOD rings.** Tile LOD is a function of Chebyshev tile distance
from the camera: ring L covers `lod0RadiusTiles << L` tiles. Each LOD halves
the heightmap resolution *and* the mesh patch grid. Defaults (512m tiles,
LOD0 = 512 segments = 1m/texel):

| LOD | ring (tiles) | heightmap | mesh patches | ~texel size |
|-----|--------------|-----------|--------------|-------------|
| 0   | ≤ 2          | 515²      | 64²          | 1 m         |
| 1   | ≤ 4          | 259²      | 32²          | 2 m         |
| 2   | ≤ 8          | 131²      | 16²          | 4 m         |
| 3   | rest         | 67²       | 8²           | 8 m         |

GPU tessellation (`terrain.tesc`, distance-based, up to 32×) further
subdivides near patches, so effective ground resolution near the camera is
sub-metre. Worst-case GPU heightmap memory for the full 10km world is
~20MB — texture streaming is *not* needed for heights at this scale.

**Async generation, budgeted upload.** Tile heightmaps are generated on
worker threads (`JobSystem`), polled via atomics (never blocking the frame),
and integrated under a per-frame budget (`uploadsPerFrame`). Requests are
prioritized: missing tiles first (closest first), then LOD upgrades, then
downgrades (with one ring of hysteresis so tiles don't thrash at ring
boundaries). A tile being upgraded keeps showing its old LOD until the new
data is fully ready — refinement is a swap, never a hole.

**Seam-free borders (the hard part of tiling).**
- Heightmaps carry a **1-texel gutter**: the grid is `(n+3)²` texels for `n`
  segments, sampled in *world* coordinates, so two adjacent tiles store
  bit-identical heights along their shared border and identical
  central-difference normals (the gutter provides the off-tile neighbors).
  Mesh UVs are baked to address the gutter interior exactly on texel centers
  (`MeshBuilder::CreateTerrainTile`).
- Tessellation factors are computed from **world-space** edge midpoints
  (`terrain.tesc` fix), so two patches meeting at a border pick identical
  factors → watertight within and across same-LOD tiles.
- Across *different* LODs, tiles wear perimeter **skirts** (the
  industry-standard crack cover — depth auto-scaled per LOD). Detail layers
  now tile in **world space** (`terrain.frag`), so texture phase is
  continuous across tiles.

**Physics ring.** LOD0 tiles keep their CPU height grid; a small pool of
Bullet `btHeightfieldTerrainShape` colliders follows the camera tile
(`colliderRadiusTiles`), re-filled in place via `SyncFromHeightField` — no
per-move shape allocation. `TerrainStreamer::GetHeight()` gives exact CPU
height queries anywhere (used by the demo's ground clamp).

**Gaea-grade sources, reserved splat space.** The height source is a
pluggable `heightFn(wx, wz) → [0,1]` called in world metres on worker
threads — the default is OpenSimplex2 FBm, but a Gaea tiled-export sampler or
any erosion-simulating generator plugs in without touching the streamer.
Splat is reserved end-to-end: shared detail layers (`layers`, up to 4 albedo
+ normal, world-continuous tiling) plus an optional per-tile `splatFn(worldMin,
worldMax, res) → RGBA8` generated alongside heights and uploaded per tile
(`AssetManager::CreateOrUpdateTextureRGBA8` recycles slot textures — no cache
growth).

### Answering the design questions

**"只做地形載入，應該有機會很快做到吧？"** — Yes, and Phase 1 is exactly
that. Heightfield terrain is the *easy* 80% of open-world streaming because
the data is regular: procedural or image tiles, fixed-size, trivially
generated/loaded async, LOD is just resolution. What makes GoT/BotW hard is
everything *on* the terrain (objects, vegetation, HLOD proxies, texture
pools). Terrain-only at 10km is achievable with the current engine, as this
phase shows.

**Do we need HLOD?** Not for the terrain itself — the LOD ring system *is*
the terrain's HLOD (a coarse tile is exactly a merged proxy of its detailed
self). HLOD becomes necessary the moment distant *objects* (trees, rocks,
buildings) must be visible: you merge clusters of objects into single baked
proxy meshes/impostors per region and swap them against real objects by
distance. That is Phase 3, and the tile grid built here is the natural HLOD
partitioning.

**Do we need texture streaming / virtual texturing?** For *heights* at
10km — no (see the ~20MB figure). For *splat + detail albedo* it depends on
scale:
- Phase-1 approach (per-tile splat textures + a few shared detail layers) is
  exactly Ghost of Tsushima's class of solution and comfortably covers 10km.
- Full-terrain *unique* color (Gaea texture maps at 1m/texel = 10240² ≈
  400MB+) needs streamed mip tiles — that's **Sparse/Streaming Virtual
  Texturing** (Phase 4). VT is the right call when you want baked erosion
  coloring, decals, and per-texel uniqueness like GoT's macro maps; it is
  overkill while splat + tiled details suffice.

---

## Phase 2 — streaming robustness & scale

- **Disk-backed tile cache**: bake generated tiles (LZ4-compressed R16) to a
  cache dir; load beats generate. This is also the Gaea import path: a
  pre-tiled pyramid (like a web-map quadtree) exported from Gaea's tiled
  build, streamed by the same requests. → true GoT loading (pure IO, no
  synthesis) with `memcpy`-speed decompress.
- **Larger worlds / origin shifting**: >20km needs camera-relative rendering
  (float precision dies ~16km) and a sparse tile map instead of
  always-resident (drop far tiles to disk, keep a low-res "world mip").
- **Geomorphing (CDLOD-style)**: blend vertex heights between LOD levels in
  the shader near ring boundaries to eliminate the (already subtle)
  LOD-switch pop entirely — the tese shader already samples the heightmap, so
  it needs only the coarser mip + a morph factor.
- **Shared tile vertex buffers**: tiles currently own per-slot VBs because
  draw batching keys material off the mesh; add per-instance material binding
  to the TERRAIN pass and all tiles of an LOD share one grid mesh
  (~25MB → ~1MB, and instanced draws).

## Phase 3 — the world on top (HLOD)

- Deterministic per-tile scatter (trees/rocks/grass from splat weights +
  noise, seeded by tile coord) streamed with the tile.
- Near ring: real instanced meshes; mid ring: impostor billboards; far ring:
  baked **HLOD proxy** per tile (one merged mesh + one baked atlas texture),
  generated offline or on first visit. Tile grid = HLOD cluster grid.
- GPU instance culling for vegetation (compute frustum + hi-z).

## Phase 4 — texture pipeline end-game

- **Streaming Virtual Texturing** for unique terrain color/splat: page table
  texture + physical page atlas, feedback pass requests pages, worker
  transcodes (BCn) and uploads. Gives GoT-style hand/erosion-painted ground
  at any world size with a fixed memory pool.
- Detail layer arrays (`GL_TEXTURE_2D_ARRAY`) with anti-tiling (stochastic
  texturing), triplanar on steep slopes, height-blended splat.

## Phase 5 — GPU-driven terrain

- Quadtree selection + tessellation factors computed in compute shaders,
  indirect draws (no CPU per-tile cost at all); or mesh-shader path where
  available. The Phase-1 data model (tile pyramid) is unchanged — only the
  traversal moves to the GPU.

---

## Tuning notes

- `lod0RadiusTiles` and `tileHeightRes` trade near-field density vs memory;
  1m/texel LOD0 matches Gaea's typical 4K-over-4km exports.
- `uploadsPerFrame` bounds worst-case frame cost of integration (each LOD0
  upload is a 515² R16 `glTexImage2D` ≈ 0.5MB).
- Sprint through the world (demo key `E`, ~1.2km/s) to stress re-ringing;
  `pending` in the stats line should drain to zero shortly after you stop.
