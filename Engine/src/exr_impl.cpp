// Single translation unit that compiles the tinyexr implementation for loading
// equirectangular .exr environment maps (AssetManager::LoadHDR dispatches to it
// by extension).
//
// tinyexr (v1.0.1, self-contained single header) uses miniz for zlib
// (de)compression; miniz itself is vendored as external/miniz and compiled as a
// separate C TU (miniz.c). TINYEXR_USE_MINIZ defaults to 1, so tinyexr.h pulls
// in <miniz.h> for the declarations and links against that miniz.c.
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
