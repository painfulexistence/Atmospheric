#include "tilemap_2d.hpp"
#include "graphics_subsystem.hpp"
#include <algorithm>

Tilemap2D::Tilemap2D(const Tilemap2DData& data, uint32_t tilesetTexID) : _data(data), _tilesetTexID(tilesetTexID) {
}

void Tilemap2D::Draw(GraphicsSubsystem* gfx, float camX, float camY, int screenW, int screenH) const {
    const int ts = _data.tileSize;
    const int cols = _data.tilesetCols;
    const int rows = _data.tilesetRows;

    const int startCol = std::max(0, static_cast<int>(camX / ts));
    const int startRow = std::max(0, static_cast<int>(camY / ts));
    const int endCol = std::min(_data.width, startCol + screenW / ts + 2);
    const int endRow = std::min(_data.height, startRow + screenH / ts + 2);

    for (int row = startRow; row < endRow; row++) {
        for (int col = startCol; col < endCol; col++) {
            int tileIdx = _data.tiles[row * _data.width + col];
            if (tileIdx < 0) continue;

            float wx = static_cast<float>(col * ts) - camX;
            float wy = static_cast<float>(row * ts) - camY;

            gfx->DrawTile(
                wx,
                wy,
                static_cast<float>(ts),
                static_cast<float>(ts),
                _tilesetTexID,
                glm::vec2(static_cast<float>(cols), static_cast<float>(rows)),
                tileIdx % cols,
                tileIdx / cols
            );
        }
    }
}

bool Tilemap2D::IsSolidWorld(float wx, float wy) const {
    if (wx < 0 || wy < 0) return true;
    int col = static_cast<int>(wx / _data.tileSize);
    int row = static_cast<int>(wy / _data.tileSize);
    if (col >= _data.width || row >= _data.height) return true;
    int tileIdx = _data.tiles[row * _data.width + col];
    return _data.solid.contains(tileIdx);
}
