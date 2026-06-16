#include "tilemap_2d.hpp"
#include "graphics_server.hpp"
#include <algorithm>

Tilemap2D::Tilemap2D(const Tilemap2DData& data, uint32_t tilesetTexID)
    : _data(data), _tilesetTexID(tilesetTexID) {}

void Tilemap2D::Draw(GraphicsServer* gfx,
                     float camX, float camY,
                     int screenW, int screenH) const {
    const int ts   = _data.tileSize;
    const int cols = _data.tilesetCols;
    const int rows = _data.tilesetRows;

    const int startCol = std::max(0, (int)(camX / ts));
    const int startRow = std::max(0, (int)(camY / ts));
    const int endCol   = std::min(_data.width,  startCol + screenW / ts + 2);
    const int endRow   = std::min(_data.height, startRow + screenH / ts + 2);

    for (int row = startRow; row < endRow; row++) {
        for (int col = startCol; col < endCol; col++) {
            int tileIdx = _data.tiles[row * _data.width + col];
            if (tileIdx < 0) continue;

            float wx = (float)(col * ts) - camX;
            float wy = (float)(row * ts) - camY;

            gfx->DrawTile(wx, wy, (float)ts, (float)ts,
                          _tilesetTexID,
                          glm::vec2((float)cols, (float)rows),
                          tileIdx % cols,
                          tileIdx / cols);
        }
    }
}

bool Tilemap2D::IsSolidWorld(float wx, float wy) const {
    if (wx < 0 || wy < 0) return true;
    int col = (int)(wx / _data.tileSize);
    int row = (int)(wy / _data.tileSize);
    if (col >= _data.width || row >= _data.height) return true;
    int tileIdx = _data.tiles[row * _data.width + col];
    return _data.solid.count(tileIdx) > 0;
}
