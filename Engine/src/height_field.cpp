#include "height_field.hpp"
#include "file_system.hpp"
#include "FastNoiseLite.h"
#include "stb_image.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>

// ----------------------------------------------------------------------------
// HeightField
// ----------------------------------------------------------------------------

float HeightField::SampleNormalized(float u, float v) const {
    const int w = Width(), d = Depth();
    const float x = std::clamp(u, 0.0f, 1.0f) * float(w - 1);
    const float z = std::clamp(v, 0.0f, 1.0f) * float(d - 1);
    const int   x0 = (int)x, z0 = (int)z;
    const int   x1 = std::min(x0 + 1, w - 1), z1 = std::min(z0 + 1, d - 1);
    const float fx = x - float(x0), fz = z - float(z0);
    const float top    = Sample(x0, z0) * (1.0f - fx) + Sample(x1, z0) * fx;
    const float bottom = Sample(x0, z1) * (1.0f - fx) + Sample(x1, z1) * fx;
    return top * (1.0f - fz) + bottom * fz;
}

// ----------------------------------------------------------------------------
// ImageHeightField
// ----------------------------------------------------------------------------

static bool HasExtension(const std::string& path, const char* ext) {
    const size_t extLen = std::strlen(ext);
    if (path.size() < extLen) return false;
    for (size_t i = 0; i < extLen; ++i) {
        if (std::tolower(path[path.size() - extLen + i]) != ext[i]) return false;
    }
    return true;
}

ImageHeightField::ImageHeightField(const std::string& path) {
    FileSystem::Bytes bytes = FileSystem::Get().ReadSync(path);
    if (bytes.empty())
        throw std::runtime_error("ImageHeightField: cannot read file: " + path);

    if (HasExtension(path, ".r16") || HasExtension(path, ".raw"))
        LoadRaw16(bytes, path);
    else if (HasExtension(path, ".r32"))
        LoadRaw32(bytes, path);
    else
        LoadStb(bytes, path);

    if (_width != _depth) {
        throw std::runtime_error("ImageHeightField: Heightmap at '" + path +
                                 "' must be square! Dimensions: " +
                                 std::to_string(_width) + "x" + std::to_string(_depth));
    }
}

// Headerless square little-endian uint16 (Gaea .r16 / WorldCreator RAW export).
void ImageHeightField::LoadRaw16(const std::vector<unsigned char>& bytes, const std::string& path) {
    const size_t count = bytes.size() / sizeof(uint16_t);
    const int    side  = (int)std::lround(std::sqrt((double)count));
    if (count == 0 || (size_t)side * side * sizeof(uint16_t) != bytes.size())
        throw std::runtime_error("ImageHeightField: '" + path + "' is not a square 16-bit RAW heightmap");

    _width = _depth = side;
    _grid.resize(count);
    // Row 0 of a RAW export is the top edge; flip vertically to match the
    // stb_image path (which loads with flip_vertically_on_load).
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            uint16_t v;
            std::memcpy(&v, &bytes[((size_t)z * side + x) * sizeof(uint16_t)], sizeof(uint16_t));
            _grid[(size_t)(side - 1 - z) * side + x] = v / 65535.0f;
        }
    }
}

// Headerless square little-endian float32 in [0,1] (Gaea .r32 export).
void ImageHeightField::LoadRaw32(const std::vector<unsigned char>& bytes, const std::string& path) {
    const size_t count = bytes.size() / sizeof(float);
    const int    side  = (int)std::lround(std::sqrt((double)count));
    if (count == 0 || (size_t)side * side * sizeof(float) != bytes.size())
        throw std::runtime_error("ImageHeightField: '" + path + "' is not a square 32-bit RAW heightmap");

    _width = _depth = side;
    _grid.resize(count);
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            float v;
            std::memcpy(&v, &bytes[((size_t)z * side + x) * sizeof(float)], sizeof(float));
            _grid[(size_t)(side - 1 - z) * side + x] = std::clamp(v, 0.0f, 1.0f);
        }
    }
}

// PNG/JPG/... via stb_image; prefers the 16-bit decode path when the file
// carries 16-bit data (16-bit PNG), otherwise falls back to 8-bit.
void ImageHeightField::LoadStb(const std::vector<unsigned char>& bytes, const std::string& path) {
    const auto* data = bytes.data();
    const int   len  = (int)bytes.size();
    int w = 0, h = 0, ch = 0;

    stbi_set_flip_vertically_on_load(true);
    if (stbi_is_16_bit_from_memory(data, len)) {
        stbi_us* pixels = stbi_load_16_from_memory(data, len, &w, &h, &ch, 1);
        if (!pixels)
            throw std::runtime_error("ImageHeightField: cannot decode 16-bit image: " + path);
        _width = w; _depth = h;
        _grid.resize((size_t)w * h);
        for (size_t i = 0; i < _grid.size(); ++i)
            _grid[i] = pixels[i] / 65535.0f;
        stbi_image_free(pixels);
    } else {
        stbi_uc* pixels = stbi_load_from_memory(data, len, &w, &h, &ch, 1);
        if (!pixels)
            throw std::runtime_error("ImageHeightField: cannot decode image: " + path);
        _width = w; _depth = h;
        _grid.resize((size_t)w * h);
        for (size_t i = 0; i < _grid.size(); ++i)
            _grid[i] = pixels[i] / 255.0f;
        stbi_image_free(pixels);
    }
}

float ImageHeightField::Sample(int xi, int zi) const {
    return _grid[zi * _width + xi];
}

// ----------------------------------------------------------------------------
// NoiseHeightField
// ----------------------------------------------------------------------------

NoiseHeightField::NoiseHeightField(const NoiseHeightFieldParams& p)
    : _params(p) {
    Regenerate();
}

void NoiseHeightField::Regenerate() {
    FastNoiseLite noise;
    noise.SetSeed(_params.seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.SetFrequency(_params.frequency);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(_params.octaves);
    noise.SetFractalLacunarity(_params.lacunarity);
    noise.SetFractalGain(_params.gain);

    const int res = _params.resolution;
    _grid.resize(res * res);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            float v = noise.GetNoise((float)x, (float)z);
            _grid[z * res + x] = (v + 1.0f) * 0.5f;  // map [-1,1] → [0,1]
        }
    }
}

float NoiseHeightField::Sample(int xi, int zi) const {
    return _grid[zi * _params.resolution + xi];
}
