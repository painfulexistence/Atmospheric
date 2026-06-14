#include "asset_manager.hpp"
#include <unordered_set>
#include <spdlog/spdlog.h>
#include "console.hpp"
#include "file_system.hpp"
#include "job_system.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include "shader.hpp"

#include "fmt/core.h"

// TODO: enable SIMD again after fixing build issues on Windows and Linux (ref:
// https://facebook.github.io/facebook360_dep/source/html/stb__image_8h_source.html)
#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// #define TINYOBJLOADER_IMPLEMENTATION
// #include "tiny_obj_loader.h"

#ifdef AE_USE_BASIS_UNIVERSAL
// Basis Universal transcoder — KTX2 / BasisLZ / UASTC → GPU compressed texture
// Only the transcoder is compiled; no encoder dependency.
#include "basisu_transcoder.h"

// ── ETC2 constants (part of GLES3 core; defined in GLES3/gl3.h for Emscripten,
//    and available on desktop via GL_ARB_ES3_compatibility / OpenGL 4.3+).
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2           0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC      0x9278
#endif
// ── S3TC / DXT constants (desktop, via GL_EXT_texture_compression_s3tc)
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3
#endif

namespace {
// Returns true if the named GL/WebGL extension is exposed by the current context.
bool HasGLExtension(const char* name) {
#ifdef __EMSCRIPTEN__
    // In WebGL2 (GLES3) glGetString(GL_EXTENSIONS) is deprecated; use glGetStringi.
    GLint numExt = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
    for (GLint i = 0; i < numExt; ++i) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && strcmp(ext, name) == 0) return true;
    }
    return false;
#else
    const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    return exts && (strstr(exts, name) != nullptr);
#endif
}

// Returns bytes per block for the chosen basisu target format.
uint32_t BasisBytesPerBlock(basist::transcoder_texture_format fmt) {
    switch (fmt) {
    case basist::transcoder_texture_format::cTFETC2_RGBA:
    case basist::transcoder_texture_format::cTFBC3_RGBA:
        return 16; // 128-bit blocks
    case basist::transcoder_texture_format::cTFETC1_RGB:
    case basist::transcoder_texture_format::cTFBC1_RGB:
        return 8;  //  64-bit blocks
    default:
        return 16;
    }
}

// Maps a basisu target format to the matching GL compressed internal format.
GLenum BasisToGLFormat(basist::transcoder_texture_format fmt) {
    switch (fmt) {
    case basist::transcoder_texture_format::cTFETC2_RGBA: return GL_COMPRESSED_RGBA8_ETC2_EAC;
    case basist::transcoder_texture_format::cTFETC1_RGB:  return GL_COMPRESSED_RGB8_ETC2; // ETC1 ⊂ ETC2
    case basist::transcoder_texture_format::cTFBC3_RGBA:  return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    case basist::transcoder_texture_format::cTFBC1_RGB:   return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    default:                                              return GL_COMPRESSED_RGBA8_ETC2_EAC;
    }
}
} // anonymous namespace
#endif // AE_USE_BASIS_UNIVERSAL


AssetManager* AssetManager::instance = nullptr;

AssetManager& AssetManager::Get() {
    if (!instance) {
        instance = new AssetManager();
    }
    return *instance;
}

AssetManager::~AssetManager() {
    Clear();
}

void AssetManager::Init() {
#ifdef AE_USE_BASIS_UNIVERSAL
    if (!_basisuInitialized) {
        basist::basisu_transcoder_init();
        _basisuInitialized = true;
        ENGINE_LOG("Basis Universal transcoder initialized (KTX2 support active)");
    }
#endif
    ENGINE_LOG("AssetManager initialized");
}

void AssetManager::Shutdown() {
    Clear();
    ENGINE_LOG("AssetManager shutdown");
}

void AssetManager::Clear() {
    // Clean up textures
    if (!textures.empty()) {
        glDeleteTextures(textures.size(), textures.data());
        textures.clear();
    }
    if (!defaultTextures.empty()) {
        glDeleteTextures(defaultTextures.size(), defaultTextures.data());
        defaultTextures.clear();
    }
    _textureCache.clear();
    _nextTextureID = 0;

    // Clean up shaders
    for (auto* shader : shaders) {
        delete shader;
    }
    shaders.clear();
    _shaderCache.clear();
    _nextShaderID = 0;

    // Clean up meshes
    for (auto* mesh : meshes) {
        delete mesh;
    }
    meshes.clear();
    _meshCache.clear();

    // Clean up materials
    for (auto* material : materials) {
        delete material;
    }
    materials.clear();
    _materialCache.clear();
    _nextMaterialID = 0;

    // Clear CPU cache
    _imageCache.clear();
}

void AssetManager::ClearSceneAssets() {
    // Textures: clear scene textures, keep defaultTextures untouched.
    if (!textures.empty()) {
        glDeleteTextures(textures.size(), textures.data());
        textures.clear();
    }
    // Remove scene texture cache entries (keep default texture entries by glID).
    std::unordered_set<GLuint> defaultIDs(defaultTextures.begin(), defaultTextures.end());
    for (auto it = _textureCache.begin(); it != _textureCache.end(); ) {
        if (defaultIDs.count(it->second.glID))
            ++it;
        else
            it = _textureCache.erase(it);
    }

    // Shaders: delete only scene shaders (indices >= _defaultShaderCount).
    for (uint32_t i = _defaultShaderCount; i < (uint32_t)shaders.size(); ++i)
        delete shaders[i];
    shaders.resize(_defaultShaderCount);
    // Rebuild shader cache to only contain default shaders.
    for (auto it = _shaderCache.begin(); it != _shaderCache.end(); )
        it = (it->second < _defaultShaderCount) ? std::next(it) : _shaderCache.erase(it);
    _nextShaderID = _defaultShaderCount;

    // Materials and meshes are always scene-specific.
    for (auto* m : materials) delete m;
    materials.clear();
    _materialCache.clear();
    _nextMaterialID = 0;

    for (auto* m : meshes) delete m;
    meshes.clear();
    _meshCache.clear();

    _imageCache.clear();
}

// ============================================================================
// Image Management
// ============================================================================

std::shared_ptr<Image> AssetManager::LoadImage(const std::string& path) {
    // Check cache first
    auto it = _imageCache.find(path);
    if (it != _imageCache.end()) {
        return it->second;
    }

    // Read raw bytes via FileSystem to support transparent web prefetching
    FileSystem::Bytes fileData = FileSystem::Get().ReadSync(path);
    if (fileData.empty()) {
        spdlog::warn("AssetManager::LoadImage: Failed to read file bytes via FileSystem at '{}'", path);
        return nullptr;
    }

    int width, height, numChannels;
    if (!stbi_info_from_memory(fileData.data(), (int)fileData.size(), &width, &height, &numChannels)) {
        spdlog::warn("stbi_info_from_memory: Failed to read image metadata at '{}'", path);
        return nullptr;
    }

    int desiredChannels = 0;
    switch (numChannels) {
    case 1:
        desiredChannels = 1;
        break;
    case 3:
        desiredChannels = 4;
        break;
    case 4:
        desiredChannels = 4;
        break;
    default:
        throw std::runtime_error(fmt::format("Unknown texture format at {}\n", path));
        break;
    }

    uint8_t* data = stbi_load_from_memory(fileData.data(), (int)fileData.size(), &width, &height, &numChannels, desiredChannels);
    if (data) {
        auto image = std::make_shared<Image>(width, height, desiredChannels, data);
        stbi_image_free(data);
        _imageCache[path] = image;
        return image;
    } else {
        return nullptr;
    }
}

// ============================================================================
// Shader Management
// ============================================================================

void AssetManager::LoadDefaultShaders() {
    LoadShaders({ {
                    "color",
                    { .vert = "assets/shaders/tbn.vert", .frag = "assets/shaders/pbr.frag" },
                  },
                  { "debug_line",
                    {
                      .vert = "assets/shaders/debug.vert",
                      .frag = "assets/shaders/flat.frag",
                    } },
                  {
                    "depth",
                    { .vert = "assets/shaders/depth_simple.vert", .frag = "assets/shaders/depth_simple.frag" },
                  },
                  {
                    "depth_cubemap",
                    { .vert = "assets/shaders/depth_cubemap.vert", .frag = "assets/shaders/depth_cubemap.frag" },
                  },
                  {
                    "hdr",
                    { .vert = "assets/shaders/hdr.vert", .frag = "assets/shaders/hdr.frag" },
                  },
#ifdef __EMSCRIPTEN__
                  {
                    "terrain",
                    { .vert = "assets/shaders/simple.vert",
                      .frag = "assets/shaders/flat.frag" },
                  },
#else
                  {
                    "terrain",
                    { .vert = "assets/shaders/terrain.vert",
                      .frag = "assets/shaders/terrain.frag",
                      .tesc = "assets/shaders/terrain.tesc",
                      .tese = "assets/shaders/terrain.tese" },
                  },
#endif
                  { "canvas", { .vert = "assets/shaders/canvas.vert", .frag = "assets/shaders/canvas.frag" } },
                  { "geometry", { .vert = "assets/shaders/geometry.vert", .frag = "assets/shaders/geometry.frag" } },
                  { "lighting", { .vert = "assets/shaders/lighting.vert", .frag = "assets/shaders/lighting.frag" } },
                  { "skybox",          { .vert = "assets/shaders/skybox.vert",           .frag = "assets/shaders/skybox.frag" } },
                  { "sun",             { .vert = "assets/shaders/sun.vert",              .frag = "assets/shaders/sun.frag" } },
                  { "voxel",           { .vert = "assets/shaders/voxel.vert",            .frag = "assets/shaders/voxel.frag" } },
                  { "water",           { .vert = "assets/shaders/water.vert",            .frag = "assets/shaders/water.frag" } },
                  { "bloom_threshold", { .vert = "assets/shaders/bloom.vert",            .frag = "assets/shaders/bloom_threshold.frag" } },
                  { "bloom_downsample",{ .vert = "assets/shaders/bloom.vert",            .frag = "assets/shaders/bloom_downsample.frag" } },
                  { "bloom_upsample",  { .vert = "assets/shaders/bloom.vert",            .frag = "assets/shaders/bloom_upsample.frag" } },
                  { "bloom_composite", { .vert = "assets/shaders/bloom.vert",            .frag = "assets/shaders/bloom_composite.frag" } } });
    _defaultShaderCount = (uint32_t)shaders.size();
}

void AssetManager::LoadShaders(const std::unordered_map<std::string, ShaderProgramProps>& shaderDefs) {
    for (const auto& [name, props] : shaderDefs) {
        CreateShader(name, props);
    }
}

ShaderProgram* AssetManager::CreateShader(const std::string& name, const ShaderProgramProps& props) {
    // Check if already exists
    auto it = _shaderCache.find(name);
    if (it != _shaderCache.end()) {
        ENGINE_LOG("Shader '{}' already exists, returning existing shader", name);
        return shaders[it->second];
    }

    auto* shader = new ShaderProgram(props);
    shaders.push_back(shader);
    _shaderCache[name] = _nextShaderID++;

    ENGINE_LOG("Shader '{}' loaded", name);
    return shader;
}

ShaderProgram* AssetManager::GetShader(const std::string& name) const {
    auto it = _shaderCache.find(name);
    if (it != _shaderCache.end()) {
        return shaders[it->second];
    }
    throw std::runtime_error(fmt::format("Shader '{}' not found", name));
}

ShaderProgram* AssetManager::GetShaderByID(uint32_t id) const {
    if (id < shaders.size()) {
        return shaders[id];
    }
    throw std::runtime_error(fmt::format("Shader ID {} out of range", id));
}

void AssetManager::ReloadShaders() {
    // TODO: Implement shader hot reloading
    ENGINE_LOG("Shader reloading not yet implemented");
}

// ============================================================================
// Material Management
// ============================================================================

void AssetManager::LoadMaterials(const std::vector<MaterialProps>& materialDefs) {
    for (const auto& props : materialDefs) {
        materials.push_back(new Material(props));
    }
}

Material* AssetManager::CreateMaterial(const std::string& name, const MaterialProps& props) {
    // Check if already exists
    auto it = _materialCache.find(name);
    if (it != _materialCache.end()) {
        ENGINE_LOG("Material '{}' already exists, returning existing material", name);
        return GetMaterialByID(it->second);
    }

    auto* material = new Material(props);
    materials.push_back(material);
    _materialCache[name] = _nextMaterialID++;
    return material;
}

Material* AssetManager::CreateMaterial(const MaterialProps& props) {
    auto* material = new Material(props);
    materials.push_back(material);
    _materialCache["unnamed_" + std::to_string(_nextMaterialID++)] = _nextMaterialID;
    return material;
}

Material* AssetManager::GetMaterial(const std::string& name) const {
    auto it = _materialCache.find(name);
    if (it != _materialCache.end()) {
        return GetMaterialByID(it->second);
    }
    throw std::runtime_error(fmt::format("Material '{}' not found", name));
}

Material* AssetManager::GetMaterialByID(uint32_t id) const {
    if (id < materials.size()) {
        return materials[id];
    }
    throw std::runtime_error(fmt::format("Material ID {} out of range", id));
}

// ============================================================================
// GPU Texture Management
// ============================================================================

void AssetManager::LoadDefaultTextures() {
#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
    // Web build: use pre-compressed KTX2 variants to avoid CPU-side JPEG decode
    // and to store textures on the GPU in ETC2 format (~4× less VRAM than RGBA).
    // These bytes must already be in FileSystem cache before this function is called
    // — populate them with FileSystem::Get().Prefetch() before Application::Run().
    LoadTextures({ "assets/textures/default_diff.ktx2",
                   "assets/textures/default_norm.ktx2",
                   "assets/textures/default_ao.ktx2",
                   "assets/textures/default_rough.ktx2",
                   "assets/textures/default_metallic.ktx2" });
#else
    LoadTextures({ "assets/textures/default_diff.jpg",
                   "assets/textures/default_norm.jpg",
                   "assets/textures/default_ao.jpg",
                   "assets/textures/default_rough.jpg",
                   "assets/textures/default_metallic.jpg" });
#endif

    // Store as default textures
    defaultTextures = textures;
    textures.clear();
}

#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
// Helper to swap standard image extensions to .ktx2 under WebAssembly
static std::string RedirectToKTX2(const std::string& path) {
    // Normalize leading "./" so cache keys are consistent with FileSystem
    std::string p = (path.size() >= 2 && path[0] == '.' && path[1] == '/') ? path.substr(2) : path;
    if (p.find("aim.png") != std::string::npos || p.find("heightmap") != std::string::npos) {
        return p;
    }
    size_t extPos = p.find_last_of('.');
    if (extPos != std::string::npos) {
        std::string ext = p.substr(extPos);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
            return p.substr(0, extPos) + ".ktx2";
        }
    }
    return p;
}
#endif

void AssetManager::LoadTextures(const std::vector<std::string>& paths) {
    int oldCount = (int)textures.size();
    int newCount = (int)paths.size();

    // Reserve final slots so ordering matches input path ordering.
    textures.resize(oldCount + newCount, 0u);

    // ── Pass 1: KTX2 files (GPU-compressed; must be transcoded on the main
    //           thread because we make GL calls inside LoadKTX2Texture).
    // ── Pass 2: Regular images (parallel CPU load → batch GPU upload).
    std::vector<int>         regularIndices;
    std::vector<std::string> regularPaths;

    for (int i = 0; i < newCount; i++) {
        std::string path = paths[i];
        if (path.size() >= 2 && path[0] == '.' && path[1] == '/') path = path.substr(2);
#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
        path = RedirectToKTX2(path);
#endif
        if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".ktx2") == 0) {
#ifdef AE_USE_BASIS_UNIVERSAL
            auto cached = _textureCache.find(path);
            if (cached != _textureCache.end()) {
                textures[oldCount + i] = cached->second.glID;
            } else {
                Texture2D tex2d;
                GLuint texID = LoadKTX2Texture(path, &tex2d);
                textures[oldCount + i] = texID;
                _textureCache[path] = tex2d;
            }
#else
            throw std::runtime_error(
                fmt::format("KTX2 texture requested but AE_USE_BASIS_UNIVERSAL is disabled: {}", path));
#endif
        } else {
            regularIndices.push_back(i);
            regularPaths.push_back(path);
        }
    }

    if (regularPaths.empty()) return;

    // ── Parallel CPU image decode for regular (non-KTX2) textures
    std::vector<std::shared_ptr<Image>> images(regularPaths.size());
    for (int j = 0; j < (int)regularPaths.size(); j++) {
        auto path  = regularPaths[j];
        auto image = &images[j];
        JobSystem::Get()->Execute([this, path, image](int /*threadID*/) { *image = LoadImage(path); });
    }
    JobSystem::Get()->Wait();

    // ── Batch generate GL texture objects for regular images
    std::vector<GLuint> regularTexIDs(regularPaths.size());
    glGenTextures((GLsizei)regularPaths.size(), regularTexIDs.data());

    for (int j = 0; j < (int)regularPaths.size(); j++) {
        int i      = regularIndices[j];
        auto& img  = images[j];
        GLuint texID = regularTexIDs[j];

        if (!img) {
            spdlog::warn("Failed to load texture at '{}', using default fallback texture.", regularPaths[j]);
            // Re-use the default texture (defaultTextures[0]) as a safe fallback
            textures[oldCount + i] = defaultTextures.empty() ? 0u : defaultTextures[0];
            _textureCache[regularPaths[j]] = { textures[oldCount + i], 0, 0, 0 };
            continue;
        }

        textures[oldCount + i] = texID;

        glBindTexture(GL_TEXTURE_2D, texID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,       GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,       GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,   GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,   GL_LINEAR);

        switch (img->channelCount) {
        case 1:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,   img->width, img->height, 0,
                         GL_RED,  GL_UNSIGNED_BYTE, img->byteArray.data());
            break;
        case 3:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,  img->width, img->height, 0,
                         GL_RGB,  GL_UNSIGNED_BYTE, img->byteArray.data());
            break;
        case 4:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img->width, img->height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, img->byteArray.data());
            break;
        default:
            throw std::runtime_error(fmt::format("Unknown texture format at {}\n", regularPaths[j]));
        }
        glGenerateMipmap(GL_TEXTURE_2D);
        _textureCache[regularPaths[j]] = { texID, (uint32_t)img->width, (uint32_t)img->height,
                                           (size_t)img->width * img->height * img->channelCount };
    }
}

GLuint AssetManager::CreateTexture(const std::string& path) {
    std::string redirectedPath = path;
    if (redirectedPath.size() >= 2 && redirectedPath[0] == '.' && redirectedPath[1] == '/')
        redirectedPath = redirectedPath.substr(2);
#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
    redirectedPath = RedirectToKTX2(redirectedPath); // also normalizes "./" internally
#endif

    // Return cached texture (GLuint) if already uploaded.
    auto it = _textureCache.find(redirectedPath);
    if (it != _textureCache.end()) {
        return it->second.glID;
    }

#ifdef AE_USE_BASIS_UNIVERSAL
    // Route .ktx2 files to the GPU-compressed loader.
    if (redirectedPath.size() >= 5 && redirectedPath.compare(redirectedPath.size() - 5, 5, ".ktx2") == 0) {
        Texture2D tex2d;
        GLuint texID = LoadKTX2Texture(redirectedPath, &tex2d);
        textures.push_back(texID);
        _textureCache[redirectedPath] = tex2d;
        return texID;
    }
#endif

    // Regular image (PNG / JPG / etc.) via stb_image.
    auto image = LoadImage(redirectedPath);
    return CreateTextureFromImage(image);
}

GLuint AssetManager::CreateTextureFromImage(const std::shared_ptr<Image>& image) {
    if (!image) {
        throw std::runtime_error("Cannot create texture from null image");
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    switch (image->channelCount) {
    case 1:
        glTexImage2D(
          GL_TEXTURE_2D, 0, GL_R8, image->width, image->height, 0, GL_RED, GL_UNSIGNED_BYTE, image->byteArray.data()
        );
        break;
    case 3:
        glTexImage2D(
          GL_TEXTURE_2D, 0, GL_RGB, image->width, image->height, 0, GL_RGB, GL_UNSIGNED_BYTE, image->byteArray.data()
        );
        break;
    case 4:
        glTexImage2D(
          GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->byteArray.data()
        );
        break;
    }
    glGenerateMipmap(GL_TEXTURE_2D);

    size_t bytes = (size_t)image->width * image->height * image->channelCount;
    _textureCache["unnamed_" + std::to_string(_nextTextureID++)] = { texID, (uint32_t)image->width, (uint32_t)image->height, bytes };
    textures.push_back(texID);
    return texID;
}

GLuint AssetManager::GetTexture(const std::string& name) const {
    auto it = _textureCache.find(name);
    if (it != _textureCache.end()) {
        return it->second.glID;
    }
    throw std::runtime_error(fmt::format("Texture '{}' not found", name));
}

GLuint AssetManager::GetTextureByID(uint32_t id) const {
    if (id < textures.size()) {
        return textures[id];
    }
    throw std::runtime_error(fmt::format("Texture ID {} out of range", id));
}

std::string AssetManager::GetTexturePath(GLuint id) const {
    for (const auto& [path, tex2d] : _textureCache) {
        if (tex2d.glID == id) return path;
    }
    return "";
}


size_t AssetManager::getTotalTextureBytes() const {
    std::unordered_set<GLuint> seen;
    size_t total = 0;
    for (auto& kv : _textureCache) {
        if (kv.second.glID != 0 && seen.insert(kv.second.glID).second)
            total += kv.second.bytes;
    }
    return total;
}

#ifdef AE_USE_BASIS_UNIVERSAL
// ============================================================================
// KTX2 / Basis Universal GPU-compressed texture loader
//
// Bytes are sourced from FileSystem::Get().ConsumeSync(path):
//   Web    — cache was populated by FileSystem::Prefetch() before the app
//            starts; ConsumeSync moves the bytes out (zero-copy hand-off).
//   Native — ConsumeSync reads from disk on cache miss (no pre-fetch needed).
//
// Target format selection:
//   Emscripten/WebGL2 — ETC2 is a GLES3 required format (always available).
//   Desktop OpenGL    — S3TC (DXT5/DXT1) preferred if the extension is present,
//                       otherwise ETC2 (available on GL 4.3+ / ARB_ES3_compat).
//
// Mip-map handling:
//   KTX2 files should be encoded with all mip levels pre-generated:
//     basisu -ktx2 -mipmap texture.png
//     toktx --t2 --encode etc1s --mipmap out.ktx2 input.png
//   If the KTX2 has only one level, GL_LINEAR is used (no mip filtering).
// ============================================================================
GLuint AssetManager::LoadKTX2Texture(const std::string& path, Texture2D* out) {
    // Ensure basisu is initialised (may already be done in Init()).
    if (!_basisuInitialized) {
        basist::basisu_transcoder_init();
        _basisuInitialized = true;
    }

    // ── Obtain raw KTX2 bytes via FileSystem ─────────────────────────────────
    //
    // ConsumeSync: moves bytes out of cache + erases the entry (zero-copy
    // hand-off pattern).  On web the cache is pre-populated by Prefetch();
    // on native it falls back to a synchronous disk read.
    //
    std::vector<uint8_t> fileData = FileSystem::Get().ConsumeSync(path);

    if (fileData.empty())
        throw std::runtime_error(fmt::format("Failed to load KTX2 file: {}", path));

    // ── Parse KTX2 container ─────────────────────────────────────────────────
    basist::ktx2_transcoder ktx2Dec;
    if (!ktx2Dec.init(fileData.data(), (uint32_t)fileData.size()))
        throw std::runtime_error(fmt::format("Failed to parse KTX2 header: {}", path));

    // ── Choose transcoding target based on GL extension availability ─────────
    //
    //   WebGL2/GLES3: ETC2 is guaranteed (no extension check needed).
    //   Desktop GL:   Prefer S3TC (ubiquitous on PC), fall back to ETC2.
    //
    bool hasAlpha = ktx2Dec.get_has_alpha();

    // Unified format check: prefer S3TC (DXT) on both native and web, falling back to ETC2
    basist::transcoder_texture_format basisFmt;
    if (HasGLExtension("GL_EXT_texture_compression_s3tc")) {
        basisFmt = hasAlpha
            ? basist::transcoder_texture_format::cTFBC3_RGBA
            : basist::transcoder_texture_format::cTFBC1_RGB;
    } else {
        basisFmt = hasAlpha
            ? basist::transcoder_texture_format::cTFETC2_RGBA
            : basist::transcoder_texture_format::cTFETC1_RGB;
    }

    GLenum   glFmt        = BasisToGLFormat(basisFmt);
    uint32_t bytesPerBlk  = BasisBytesPerBlock(basisFmt);

    // ── Start transcoding ────────────────────────────────────────────────────
    if (!ktx2Dec.start_transcoding())
        throw std::runtime_error(fmt::format("KTX2 start_transcoding failed: {}", path));

    uint32_t baseWidth  = ktx2Dec.get_width();
    uint32_t baseHeight = ktx2Dec.get_height();
    uint32_t levels     = std::max(1u, ktx2Dec.get_levels());

    // ── Create GL texture object ─────────────────────────────────────────────
    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (levels > 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)(levels - 1));
    } else {
        // Single level in the KTX2 — warn the user and use bilinear.
        ENGINE_LOG("KTX2 '{}' has no pre-generated mips; encoding with -mipmap is recommended", path);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    // ── Transcode and upload each mip level ──────────────────────────────────
    for (uint32_t level = 0; level < levels; ++level) {
        basist::ktx2_image_level_info info;
        if (!ktx2Dec.get_image_level_info(info, level, 0, 0)) {
            glDeleteTextures(1, &texID);
            throw std::runtime_error(
                fmt::format("KTX2 get_image_level_info failed (level {}): {}", level, path));
        }

        uint32_t numBlocks  = info.m_total_blocks;
        uint32_t bufferSize = numBlocks * bytesPerBlk;
        std::vector<uint8_t> buf(bufferSize);

        if (!ktx2Dec.transcode_image_level(level, 0, 0, buf.data(), numBlocks, basisFmt)) {
            glDeleteTextures(1, &texID);
            throw std::runtime_error(
                fmt::format("KTX2 transcode_image_level failed (level {}): {}", level, path));
        }

        // Level dimensions (clamped to 1 for very small mips).
        GLsizei w = (GLsizei)std::max(1u, baseWidth  >> level);
        GLsizei h = (GLsizei)std::max(1u, baseHeight >> level);

        glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)level, glFmt,
                               w, h, 0, (GLsizei)bufferSize, buf.data());
    }

    ENGINE_LOG("Loaded KTX2 texture '{}' ({}×{}, {} mips, {})",
               path, baseWidth, baseHeight, levels,
               hasAlpha ? "RGBA" : "RGB");

    if (out) {
        // Sum compressed bytes across all mip levels for accurate VRAM accounting.
        size_t totalBytes = 0;
        for (uint32_t level = 0; level < levels; ++level) {
            basist::ktx2_image_level_info info;
            if (ktx2Dec.get_image_level_info(info, level, 0, 0))
                totalBytes += (size_t)info.m_total_blocks * bytesPerBlk;
        }
        *out = { texID, baseWidth, baseHeight, totalBytes };
    }

    return texID;
}
#endif // AE_USE_BASIS_UNIVERSAL

// ============================================================================
// GPU Mesh Management
// ============================================================================

Mesh* AssetManager::CreateMesh(Mesh* mesh) {
    return CreateMesh("unnamed_" + std::to_string(_nextMeshID++), mesh);
}

Mesh* AssetManager::CreateMesh(const std::string& name, Mesh* mesh) {
    if (!mesh) {
        mesh = new Mesh();
    }
    meshes.push_back(mesh);
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::CreateCubeMesh(const std::string& name, float size) {
    auto mesh = MeshBuilder::CreateCube(size);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::CreatePlaneMesh(const std::string& name, float width, float height) {
    auto mesh = MeshBuilder::CreatePlane(width, height);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::CreatePlaneMeshSubdivided(const std::string& name,
                                               float width, float height, int subdivisions) {
    int n = std::max(1, subdivisions);
    float hw = width * 0.5f, hh = height * 0.5f;

    std::vector<Vertex> verts;
    std::vector<uint16_t> tris;
    verts.reserve((n + 1) * (n + 1));
    tris.reserve(n * n * 6);

    for (int z = 0; z <= n; ++z) {
        for (int x = 0; x <= n; ++x) {
            float fx = -hw + width  * x / n;
            float fz = -hh + height * z / n;
            verts.push_back({ { fx, 0.0f, fz },
                              { (float)x / n, (float)z / n },
                              { 0.0f, 1.0f, 0.0f } });
        }
    }
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            uint16_t i0 = (uint16_t)( z      * (n + 1) + x    );
            uint16_t i1 = (uint16_t)( z      * (n + 1) + x + 1);
            uint16_t i2 = (uint16_t)((z + 1) * (n + 1) + x    );
            uint16_t i3 = (uint16_t)((z + 1) * (n + 1) + x + 1);
            tris.insert(tris.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }

    auto mesh = new Mesh(MeshType::PRIM);
    mesh->Initialize(verts, tris);
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::CreateSphereMesh(const std::string& name, float radius, int division) {
    auto mesh = MeshBuilder::CreateSphere(radius, division);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::CreateCapsuleMesh(const std::string& name, float radius, float height) {
    // TODO: Implement capsule mesh generation
    ENGINE_LOG("Capsule mesh '{}' created (generation not yet implemented)", name);
    auto mesh = new Mesh();
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::CreateTerrainMesh(const std::string& name, float worldSize, int resolution) {
    auto mesh = MeshBuilder::CreateTerrain(worldSize, resolution);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    _meshCache[name] = mesh;
    return mesh;
}

Mesh* AssetManager::GetMesh(const std::string& name) const {
    auto it = _meshCache.find(name);
    if (it != _meshCache.end()) {
        return it->second;
    }
    throw std::runtime_error(fmt::format("Mesh '{}' not found", name));
}

std::shared_ptr<Mesh> AssetManager::LoadOBJ(const std::string& path) {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    // tinyobj::attrib_t attrib;
    // std::vector<tinyobj::shape_t> shapes;
    // std::vector<tinyobj::material_t> materials;
    // std::string err;

    // if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str())) {
    //     throw std::runtime_error(fmt::format("Failed to load model: {}", err));
    // }

    // for (const auto& shape : shapes) {
    //     for (const auto& index : shape.mesh.indices) {
    //         Vertex vert = {};
    //         vert.position = { attrib.vertices[3 * index.vertex_index + 0],
    //                           attrib.vertices[3 * index.vertex_index + 1],
    //                           attrib.vertices[3 * index.vertex_index + 2] };
    //         vert.normal = { attrib.normals[3 * index.normal_index + 0],
    //                         attrib.normals[3 * index.normal_index + 1],
    //                         attrib.normals[3 * index.normal_index + 2] };
    //         vert.uv = { attrib.texcoords[2 * index.texcoord_index + 0],
    //                     1.0 - attrib.texcoords[2 * index.texcoord_index + 1] };
    //         vertices.push_back(vert);
    //         indices.push_back(indices.size());
    //     }
    // }

    auto mesh = std::make_shared<Mesh>(MeshType::PRIM);
    mesh->Initialize(vertices, indices);

    return mesh;
}
