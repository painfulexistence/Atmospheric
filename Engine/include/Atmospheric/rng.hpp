#pragma once
#include <random>

namespace Atmospheric {
    class RNG {
    public:
        RNG() : rng(std::random_device{}()) {
        }

        float RandomFloat() {
            return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
        }

        float RandomFloatInRange(float min, float max) {
            return std::uniform_real_distribution<float>(min, max)(rng);
        }

    private:
        std::mt19937 rng;
    };
}// namespace Atmospheric
