#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_set>
#include <vector>

class GraphicsServer;

/// Tile-map data descriptor (mirrors 2d-engine TilemapData)
struct Tilemap2DData {
    int width       = 0;    ///< tiles across
    int height      = 0;    ///< tiles down
    int tileSize    = 32;   ///< pixels per tile (square)
    int tilesetCols = 1;    ///< columns in the tileset texture
    int tilesetRows = 1;    ///< rows    in the tileset texture
    std::vector<int>          tiles;  ///< row-major tile indices (0-based)
    std::unordered_set<int>   solid;  ///< tile indices that block movement
};

/// 2D tile-map renderer + collision helper.
/// Ported from 2d-engine/src/graphics/tilemap.ts
class Tilemap2D {
public:
    Tilemap2D(const Tilemap2DData& data, uint32_t tilesetTexID);

    /// Draw only tiles visible in the camera viewport [camX, camY, screenW, screenH].
    void Draw(GraphicsServer* gfx, float camX, float camY,
              int screenW, int screenH) const;

    /// True if the world-space pixel (wx, wy) falls on a solid tile.
    bool IsSolidWorld(float wx, float wy) const;

    int GetPixelWidth()  const { return _data.width  * _data.tileSize; }
    int GetPixelHeight() const { return _data.height * _data.tileSize; }

    const Tilemap2DData& GetData() const { return _data; }

private:
    Tilemap2DData _data;
    uint32_t      _tilesetTexID;
};
