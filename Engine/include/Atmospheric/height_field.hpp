#pragma once
#include <memory>
#include <string>
#include <vector>

// Normalized [0,1] height source shared between TerrainMeshComponent (GPU) and
// HeightFieldColliderComponent (CPU physics).  Grid() is a flat row-major array
// of Width()*Depth() values; xi is the fast (column) axis.
class HeightField {
public:
    virtual ~HeightField() = default;
    virtual int                       Width()           const = 0;
    virtual int                       Depth()           const = 0;
    virtual float                     Sample(int xi, int zi) const = 0;
    virtual const std::vector<float>& Grid()            const = 0;

    // Bilinear sample at normalized (u, v) in [0,1] — used to resample the
    // grid at a different resolution (e.g. a decimated physics collider).
    float SampleNormalized(float u, float v) const;
};

// Loads a heightmap from disk; values map to [0,1]. Supported formats:
//   .png/.jpg/...  8-bit or 16-bit via stb_image (16-bit PNG recommended —
//                  both WorldCreator and Gaea can export it)
//   .r16 / .raw    headerless little-endian uint16, square (Gaea/WorldCreator
//                  RAW export)
//   .r32           headerless little-endian float32 in [0,1], square
class ImageHeightField : public HeightField {
public:
    explicit ImageHeightField(const std::string& path);
    int   Width()  const override { return _width; }
    int   Depth()  const override { return _depth; }
    float Sample(int xi, int zi) const override;
    const std::vector<float>& Grid() const override { return _grid; }
private:
    void LoadRaw16(const std::vector<unsigned char>& bytes, const std::string& path);
    void LoadRaw32(const std::vector<unsigned char>& bytes, const std::string& path);
    void LoadStb(const std::vector<unsigned char>& bytes, const std::string& path);

    int               _width = 0, _depth = 0;
    std::vector<float> _grid;
};

struct NoiseHeightFieldParams {
    int   resolution = 128;
    int   seed       = 42;
    float frequency  = 0.0035f;
    int   octaves    = 8;
    float lacunarity = 2.0f;
    float gain       = 0.5f;

    bool operator==(const NoiseHeightFieldParams&) const = default;
};

// Procedural OpenSimplex2 FBm — same noise algorithm as VoxelWorld terrain.
class NoiseHeightField : public HeightField {
public:
    explicit NoiseHeightField(const NoiseHeightFieldParams& params = {});
    int   Width()  const override { return _params.resolution; }
    int   Depth()  const override { return _params.resolution; }
    float Sample(int xi, int zi) const override;
    const std::vector<float>& Grid() const override { return _grid; }

    // Mutable so the editor (TerrainMeshComponent::DrawImGui) can tweak
    // seed/frequency/etc. in place; call Regenerate() to rebuild the grid.
    NoiseHeightFieldParams& Params() { return _params; }
    void Regenerate();
private:
    NoiseHeightFieldParams _params;
    std::vector<float>     _grid;
};
