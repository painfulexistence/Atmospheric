#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Disk cache for streamed terrain tile heightmaps (see TerrainStreamer):
// Ghost-of-Tsushima-style loading, where baked tiles are read and uploaded
// instead of synthesized. Grids are quantized to 16 bits and compressed with
// a left-neighbor delta + zigzag varint coder — dependency-free, decodes at
// near-memcpy speed, and typically shrinks smooth terrain to 25-50% of raw.
//
// Fully portable: plain C++ file IO, safe to call from JobSystem workers
// (no shared mutable state; concurrent stores of the same tile are benign).
// On Emscripten the default filesystem is MEMFS (RAM) — point the cache at a
// preloaded/fetched read-only pyramid or a mounted IDBFS dir for persistence.
class TerrainTileCache {
public:
    // dir is created on demand; empty disables the cache (Load misses,
    // Store no-ops).
    explicit TerrainTileCache(std::string dir);

    bool Enabled() const {
        return !_dir.empty();
    }

    // Fills `out` (w*w floats in [0,1]) on hit. paramsHash must match the
    // value used at Store time — derive it from every input that shapes the
    // heights (noise params, heightScale, tile size, cache version).
    bool Load(int tileX, int tileZ, int lod, uint32_t paramsHash, int w, std::vector<float>& out) const;

    // Persists a generated grid. Skips silently if the file already exists
    // or the directory isn't writable (e.g. read-only web bundles).
    void Store(int tileX, int tileZ, int lod, uint32_t paramsHash, int w, const std::vector<float>& grid) const;

    // FNV-1a helper for building parameter hashes.
    static uint32_t HashCombine(uint32_t h, const void* data, size_t size);

private:
    std::string PathFor(int tileX, int tileZ, int lod, uint32_t paramsHash) const;

    std::string _dir;
};
