// Single translation unit that compiles the tinyexr implementation for loading
// equirectangular .exr environment maps (AssetManager::LoadHDR dispatches to it
// by extension).
//
// tinyexr's TINYEXR_USE_STB_ZLIB path reuses stb's zlib codec instead of pulling
// in miniz/zlib as a new dependency:
//   - stbi_zlib_decode_buffer  (decompress, used by LoadEXR) comes from
//     stb_image.h's STB_IMAGE_IMPLEMENTATION unit (asset_manager.cpp).
//   - stbi_zlib_compress       (compress, only referenced by the EXR *save*
//     paths) is provided here by stb_image_write's implementation, so the save
//     functions link even though we never call them.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#include "tinyexr.h"
