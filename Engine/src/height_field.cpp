#include "height_field.hpp"
#include "asset_manager.hpp"
#include "FastNoiseLite.h"
#include <stdexcept>

// ----------------------------------------------------------------------------
// ImageHeightField
// ----------------------------------------------------------------------------

ImageHeightField::ImageHeightField(const std::string& path) {
    auto img = AssetManager::Get().LoadImage(path);
    if (!img)
        throw std::runtime_error("ImageHeightField: cannot load image: " + path);
    if (img->width != img->height) {
        throw std::runtime_error("ImageHeightField: Heightmap image at '" + path + 
                                 "' must be square! Dimensions: " + 
                                 std::to_string(img->width) + "x" + std::to_string(img->height));
    }
    _width = img->width;
    _depth = img->height;
    const int n = _width * _depth;
    _grid.resize(n);
    for (int i = 0; i < n; ++i)
        _grid[i] = img->byteArray[i * img->channelCount] / 255.0f;
}

float ImageHeightField::Sample(int xi, int zi) const {
    return _grid[zi * _width + xi];
}

// ----------------------------------------------------------------------------
// NoiseHeightField
// ----------------------------------------------------------------------------

NoiseHeightField::NoiseHeightField(const NoiseHeightFieldParams& p)
    : _resolution(p.resolution) {
    FastNoiseLite noise;
    noise.SetSeed(p.seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.SetFrequency(p.frequency);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(p.octaves);
    noise.SetFractalLacunarity(p.lacunarity);
    noise.SetFractalGain(p.gain);

    _grid.resize(_resolution * _resolution);
    for (int z = 0; z < _resolution; ++z) {
        for (int x = 0; x < _resolution; ++x) {
            float v = noise.GetNoise(static_cast<float>(x), static_cast<float>(z));
            _grid[z * _resolution + x] = (v + 1.0f) * 0.5f;  // map [-1,1] → [0,1]
        }
    }
}

float NoiseHeightField::Sample(int xi, int zi) const {
    return _grid[zi * _resolution + xi];
}
