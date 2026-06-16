#include "sand_world.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr int kDispersion[] = {
    0,// Empty
    0,// Stone
    0,// Dirt
    0,// Sand
    0,// Wood
    5,// Water
    3,// Oil
    4,// Acid
    1,// Lava
};
}

int SandWorld::Density(Mat m) {
    switch (m) {
    case Mat::Empty: return 0;
    case Mat::Smoke:
    case Mat::Steam: return 1;
    case Mat::Oil: return 2;
    case Mat::Water:
    case Mat::Acid: return 3;
    case Mat::Lava: return 4;
    case Mat::Sand: return 5;
    default: return 100;// static solids never get displaced
    }
}

void SandWorld::SetCell(int x, int y, Mat m) {
    if (!InBounds(x, y)) return;
    Cell& c = C(x, y);
    c.mat = uint8_t(m);
    c.shade = uint8_t(Rand() & 0xFF);
    switch (m) {
    case Mat::Fire: c.life = uint8_t(30 + (Rand() % 50)); break;
    case Mat::Smoke: c.life = uint8_t(60 + (Rand() % 90)); break;
    case Mat::Steam: c.life = uint8_t(80 + (Rand() % 100)); break;
    default: c.life = 0; break;
    }
}

// ---------------------------------------------------------------------------
// World generation
// ---------------------------------------------------------------------------

void SandWorld::Generate(uint32_t seed) {
    rng = seed | 1u;
    cells.assign(size_t(W) * H, Cell{});

    // Surface heightmap via a bounded random walk
    std::vector<int> surf(W);
    int h = int(H * 0.42f);
    for (int x = 0; x < W; x++) {
        h += int(Rand() % 3) - 1;
        h = std::clamp(h, H / 4, (H * 3) / 5);
        surf[x] = h;
    }

    // Fill terrain below the surface with dirt
    for (int x = 0; x < W; x++) {
        for (int y = surf[x]; y < H; y++) {
            SetCell(x, y, Mat::Dirt);
        }
    }

    // Stone veins deep down
    for (int i = 0; i < 24; i++) {
        int cx = 8 + int(Rand() % (W - 16));
        int cy = surf[cx] + 25 + int(Rand() % std::max(1, H - surf[cx] - 30));
        int r = 4 + int(Rand() % 8);
        for (int y = cy - r; y <= cy + r; y++) {
            for (int x = cx - r; x <= cx + r; x++) {
                if (!InBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy <= r * r && GetMat(x, y) == Mat::Dirt) {
                    SetCell(x, y, Mat::Stone);
                }
            }
        }
    }

    // Caves: drunkard's walk tunnels
    for (int i = 0; i < 14; i++) {
        int x = 16 + int(Rand() % (W - 32));
        int y = surf[x] + 10 + int(Rand() % 60);
        for (int step = 0; step < 260; step++) {
            int r = 2 + int(Rand() % 3);
            for (int oy = -r; oy <= r; oy++) {
                for (int ox = -r; ox <= r; ox++) {
                    if (ox * ox + oy * oy > r * r) continue;
                    int px = x + ox, py = y + oy;
                    if (InBounds(px, py) && GetMat(px, py) != Mat::Stone) {
                        C(px, py) = Cell{};
                    }
                }
            }
            uint32_t d = Rand() % 8;
            x += (d == 0 || d == 4 || d == 5) ? 1 : (d == 1 || d == 6 || d == 7) ? -1 : 0;
            y += (d == 2 || d == 4 || d == 6) ? 1 : (d == 3 || d == 5 || d == 7) ? -1 : 0;
            x = std::clamp(x, 4, W - 5);
            y = std::clamp(y, surf[x] - 4, H - 5);
        }
    }

    // Liquid pools
    static const Mat kPoolMats[] = { Mat::Water, Mat::Water, Mat::Water, Mat::Oil,
                                     Mat::Oil,   Mat::Acid,  Mat::Lava };
    for (int i = 0; i < 10; i++) {
        int cx = 20 + int(Rand() % (W - 40));
        int cy = surf[cx] + 15 + int(Rand() % std::max(1, H - surf[cx] - 25));
        int r = 5 + int(Rand() % 8);
        Mat liquid = kPoolMats[Rand() % 7];
        for (int y = cy - r; y <= cy + r; y++) {
            for (int x = cx - r; x <= cx + r; x++) {
                if (!InBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > r * r || GetMat(x, y) == Mat::Stone) continue;
                SetCell(x, y, (y >= cy) ? liquid : Mat::Empty);
            }
        }
    }

    // Wood platforms floating in the air above the surface
    for (int i = 0; i < 12; i++) {
        int len = 8 + int(Rand() % 20);
        int x0 = 8 + int(Rand() % (W - len - 16));
        int y0 = std::max(8, surf[x0] - 8 - int(Rand() % 50));
        for (int x = x0; x < x0 + len; x++) {
            for (int y = y0; y < y0 + 3; y++) {
                SetCell(x, y, Mat::Wood);
            }
        }
    }

    // Sand piles near the surface
    for (int i = 0; i < 10; i++) {
        int cx = 12 + int(Rand() % (W - 24));
        int cy = surf[cx];
        int r = 3 + int(Rand() % 6);
        for (int y = cy - r; y <= cy + r; y++) {
            for (int x = cx - r; x <= cx + r; x++) {
                if (!InBounds(x, y)) continue;
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy <= r * r) SetCell(x, y, Mat::Sand);
            }
        }
    }

    // Indestructible stone frame
    for (int x = 0; x < W; x++) {
        for (int t = 0; t < 2; t++) {
            SetCell(x, t, Mat::Stone);
            SetCell(x, H - 1 - t, Mat::Stone);
        }
    }
    for (int y = 0; y < H; y++) {
        for (int t = 0; t < 2; t++) {
            SetCell(t, y, Mat::Stone);
            SetCell(W - 1 - t, y, Mat::Stone);
        }
    }

    // Player spawn clearings
    for (int p = 0; p < 2; p++) {
        int sx = (p == 0) ? W / 6 : (W * 5) / 6;
        int sy = surf[sx] - 14;
        CarveCircle(sx, sy, 10);
        // Small landing pad so players don't immediately sink into a cave
        for (int x = sx - 6; x <= sx + 6; x++) {
            for (int y = sy + 10; y < sy + 12; y++) {
                SetCell(x, y, Mat::Stone);
            }
        }
        spawnX[p] = sx;
        spawnY[p] = sy;
    }
}

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------

bool SandWorld::TryDisplace(int x, int y, int nx, int ny, uint8_t parity) {
    if (!InBounds(nx, ny)) return false;
    Cell& src = C(x, y);
    Cell& dst = C(nx, ny);
    Mat dm = Mat(dst.mat);
    if (dm != Mat::Empty && !IsGas(dm) && !IsLiquid(dm)) return false;
    if (Density(dm) >= Density(Mat(src.mat))) return false;
    std::swap(src, dst);
    dst.stamp = parity;
    src.stamp = parity;
    return true;
}

void SandWorld::Step(uint32_t tick) {
    uint8_t parity = uint8_t(tick);

    // Bottom-up pass: everything that falls (granulars, liquids) plus fire
    for (int y = H - 2; y >= 1; y--) {
        bool l2r = ((tick + uint32_t(y)) & 1) == 0;
        for (int i = 1; i < W - 1; i++) {
            int x = l2r ? i : W - 1 - i;
            Cell& c = C(x, y);
            if (c.stamp == parity) continue;
            switch (Mat(c.mat)) {
            case Mat::Sand: UpdateGranular(x, y, parity); break;
            case Mat::Water:
            case Mat::Oil:
            case Mat::Acid:
            case Mat::Lava: UpdateLiquid(x, y, parity); break;
            case Mat::Fire: UpdateFire(x, y, parity); break;
            default: break;
            }
        }
    }

    // Top-down pass: gases rise
    for (int y = 1; y < H - 1; y++) {
        bool l2r = ((tick + uint32_t(y)) & 1) == 0;
        for (int i = 1; i < W - 1; i++) {
            int x = l2r ? i : W - 1 - i;
            Cell& c = C(x, y);
            if (c.stamp == parity) continue;
            Mat m = Mat(c.mat);
            if (IsGas(m)) UpdateGas(x, y, parity);
        }
    }
}

void SandWorld::UpdateGranular(int x, int y, uint8_t parity) {
    if (TryDisplace(x, y, x, y + 1, parity)) return;
    int d = (Rand() & 1) ? 1 : -1;
    if (TryDisplace(x, y, x + d, y + 1, parity)) return;
    TryDisplace(x, y, x - d, y + 1, parity);
}

void SandWorld::UpdateLiquid(int x, int y, uint8_t parity) {
    Cell& c = C(x, y);
    Mat m = Mat(c.mat);

    if (m == Mat::Lava) {
        // Lava interacts with neighbors before (slow) movement
        static const int nx[] = { 0, 0, -1, 1 };
        static const int ny[] = { -1, 1, 0, 0 };
        for (int n = 0; n < 4; n++) {
            Mat nm = GetMat(x + nx[n], y + ny[n]);
            if (nm == Mat::Water) {
                SetCell(x + nx[n], y + ny[n], Mat::Steam);
                SetCell(x, y, Mat::Stone);
                C(x, y).stamp = parity;
                return;
            }
            if ((nm == Mat::Wood || nm == Mat::Oil) && (Rand() & 7) == 0) {
                SetCell(x + nx[n], y + ny[n], Mat::Fire);
            }
        }
        // Lava is viscous: only moves on some ticks
        if (((uint32_t(parity) + c.shade) & 1) != 0) return;
    }

    if (m == Mat::Acid) {
        static const int nx[] = { 0, 0, -1, 1 };
        static const int ny[] = { -1, 1, 0, 0 };
        for (int n = 0; n < 4; n++) {
            Mat nm = GetMat(x + nx[n], y + ny[n]);
            if ((nm == Mat::Dirt || nm == Mat::Sand || nm == Mat::Wood) && (Rand() & 15) == 0) {
                SetCell(x + nx[n], y + ny[n], Mat::Empty);
                if ((Rand() & 3) == 0) {
                    C(x, y) = Cell{};// acid is consumed
                    return;
                }
            }
        }
    }

    if (m == Mat::Water) {
        static const int nx[] = { 0, 0, -1, 1 };
        static const int ny[] = { -1, 1, 0, 0 };
        for (int n = 0; n < 4; n++) {
            if (GetMat(x + nx[n], y + ny[n]) == Mat::Fire) {
                SetCell(x + nx[n], y + ny[n], Mat::Steam);
            }
        }
    }

    if (TryDisplace(x, y, x, y + 1, parity)) return;
    int d = (Rand() & 1) ? 1 : -1;
    if (TryDisplace(x, y, x + d, y + 1, parity)) return;
    if (TryDisplace(x, y, x - d, y + 1, parity)) return;

    // Horizontal dispersion: move toward the farthest reachable empty cell
    int disp = kDispersion[size_t(m)];
    int moved = 0;
    for (int k = 1; k <= disp; k++) {
        Mat ahead = GetMat(x + d * k, y);
        if (ahead == Mat::Empty || IsGas(ahead)) {
            moved = k;
        } else {
            break;
        }
    }
    if (moved > 0) {
        TryDisplace(x, y, x + d * moved, y, parity);
    }
}

void SandWorld::UpdateFire(int x, int y, uint8_t parity) {
    Cell& c = C(x, y);
    if (c.life == 0) {
        SetCell(x, y, ((Rand() & 3) == 0) ? Mat::Smoke : Mat::Empty);
        C(x, y).stamp = parity;
        return;
    }
    c.life--;
    c.stamp = parity;

    static const int nx[] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    static const int ny[] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    for (int n = 0; n < 8; n++) {
        Mat nm = GetMat(x + nx[n], y + ny[n]);
        if (nm == Mat::Water) {
            SetCell(x, y, Mat::Steam);
            return;
        }
        if (nm == Mat::Wood && (Rand() & 31) == 0) SetCell(x + nx[n], y + ny[n], Mat::Fire);
        if (nm == Mat::Oil && (Rand() & 3) == 0) SetCell(x + nx[n], y + ny[n], Mat::Fire);
    }

    if (GetMat(x, y - 1) == Mat::Empty && (Rand() & 15) == 0) {
        SetCell(x, y - 1, Mat::Smoke);
    }
}

void SandWorld::UpdateGas(int x, int y, uint8_t parity) {
    Cell& c = C(x, y);
    if (c.life == 0) {
        C(x, y) = Cell{};
        return;
    }
    c.life--;
    c.stamp = parity;

    uint32_t r = Rand();
    if ((r & 3) != 0) {// gases dawdle occasionally
        if (TryDisplace(x, y, x, y - 1, parity)) return;
    }
    int d = (r & 4) ? 1 : -1;
    if (TryDisplace(x, y, x + d, y - 1, parity)) return;
    if (TryDisplace(x, y, x - d, y - 1, parity)) return;
    if ((r & 8) != 0) {
        TryDisplace(x, y, x + d, y, parity);
    }
}

// ---------------------------------------------------------------------------
// Gameplay helpers
// ---------------------------------------------------------------------------

void SandWorld::CarveCircle(int cx, int cy, int r) {
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            if (!InBounds(x, y)) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r * r) continue;
            if (GetMat(x, y) == Mat::Stone) continue;
            C(x, y) = Cell{};
        }
    }
}

void SandWorld::PaintCircle(int cx, int cy, int r, Mat m, int prob256) {
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            if (!InBounds(x, y)) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r * r) continue;
            Mat cur = GetMat(x, y);
            if (IsSolid(cur)) continue;
            if (int(Rand() & 0xFF) < prob256) SetCell(x, y, m);
        }
    }
}

void SandWorld::IgniteCircle(int cx, int cy, int r, int prob256) {
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            if (!InBounds(x, y)) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r * r) continue;
            Mat cur = GetMat(x, y);
            bool flammable = (cur == Mat::Wood || cur == Mat::Oil);
            bool flash = (cur == Mat::Empty && (dx * dx + dy * dy) > (r - 2) * (r - 2));
            if ((flammable || flash) && int(Rand() & 0xFF) < prob256) {
                SetCell(x, y, Mat::Fire);
            }
        }
    }
}

int SandWorld::Raycast(float x, float y, float dx, float dy, int maxLen, int& outX, int& outY) const {
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) {
        outX = int(x);
        outY = int(y);
        return 0;
    }
    dx /= len;
    dy /= len;
    for (int i = 0; i < maxLen; i++) {
        int cx = int(x + dx * i);
        int cy = int(y + dy * i);
        outX = cx;
        outY = cy;
        if (SolidAt(cx, cy)) return i;
    }
    return maxLen;
}

uint32_t SandWorld::Checksum() const {
    uint32_t h = 2166136261u;
    for (const Cell& c : cells) {
        h = (h ^ c.mat) * 16777619u;
    }
    return h;
}
