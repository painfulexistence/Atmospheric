#pragma once
#include <cstdint>
#include <vector>

// Deterministic falling-sand cellular automaton.
// All randomness is drawn from the embedded xorshift32 RNG, so two peers that
// start from the same seed and apply the same tick sequence stay bit-identical.
// This is what makes input-only lockstep netplay possible.

enum class Mat : uint8_t {
    Empty = 0,
    Stone,// indestructible
    Dirt,
    Sand,
    Wood,
    Water,
    Oil,
    Acid,
    Lava,
    Fire,
    Smoke,
    Steam,
    Count
};

struct Cell {
    uint8_t mat = 0;
    uint8_t life = 0;// remaining lifetime (fire/gases)
    uint8_t shade = 0;// per-cell color variation
    uint8_t stamp = 0;// tick marker to avoid double-updating a moved cell
};

class SandWorld {
public:
    static constexpr int W = 480;
    static constexpr int H = 270;

    void Generate(uint32_t seed);
    void Step(uint32_t tick);

    inline const Cell& At(int x, int y) const {
        return cells[y * W + x];
    }
    inline Mat GetMat(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) return Mat::Stone;
        return Mat(cells[y * W + x].mat);
    }

    static inline bool IsSolid(Mat m) {
        return m == Mat::Stone || m == Mat::Dirt || m == Mat::Sand || m == Mat::Wood;
    }
    static inline bool IsLiquid(Mat m) {
        return m == Mat::Water || m == Mat::Oil || m == Mat::Acid || m == Mat::Lava;
    }
    static inline bool IsGas(Mat m) {
        return m == Mat::Smoke || m == Mat::Steam;
    }

    inline bool SolidAt(int x, int y) const {
        return IsSolid(GetMat(x, y));
    }

    // Gameplay operations (must only be called from deterministic sim code)
    void SetCell(int x, int y, Mat m);
    void CarveCircle(int cx, int cy, int r);// removes everything but stone
    void PaintCircle(int cx, int cy, int r, Mat m, int prob256);// fills non-solid cells
    void IgniteCircle(int cx, int cy, int r, int prob256);// sets flammables on fire

    // Steps along a ray until a solid cell or maxLen; returns travelled length.
    int Raycast(float x, float y, float dx, float dy, int maxLen, int& outX, int& outY) const;

    uint32_t Checksum() const;

    inline uint32_t Rand() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    int spawnX[2] = { 0, 0 };
    int spawnY[2] = { 0, 0 };

    std::vector<Cell> cells;

private:
    uint32_t rng = 1;

    inline Cell& C(int x, int y) {
        return cells[y * W + x];
    }
    inline bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < W && y < H;
    }

    static int Density(Mat m);
    bool TryDisplace(int x, int y, int nx, int ny, uint8_t parity);

    void UpdateGranular(int x, int y, uint8_t parity);
    void UpdateLiquid(int x, int y, uint8_t parity);
    void UpdateFire(int x, int y, uint8_t parity);
    void UpdateGas(int x, int y, uint8_t parity);
};
