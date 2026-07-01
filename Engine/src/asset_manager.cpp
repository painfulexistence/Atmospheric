#include "asset_manager.hpp"
#include <algorithm>
#include <unordered_set>
#include "console.hpp"
#include "file_system.hpp"
#include "job_system.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include "shader.hpp"

TextureHandle::TextureHandle(const char* path) {
    *this = AssetManager::Get().CreateTexture(path);
}

TextureHandle::TextureHandle(const std::string& path) {
    *this = AssetManager::Get().CreateTexture(path);
}

#include "fmt/core.h"

// TODO: enable SIMD again after fixing build issues on Windows and Linux (ref:
// https://facebook.github.io/facebook360_dep/source/html/stb__image_8h_source.html)
#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

// tiny_gltf pulls in <windows.h> on Windows builds, which #defines LoadImage
// to LoadImageA/W — undo it so AssetManager::LoadImage below isn't mangled.
#ifdef _WIN32
#undef LoadImage
#endif

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

    // Clean up meshes (skip entries registered via RegisterMesh — not owned by AssetManager)
    for (uint32_t id : _ownedMeshIDs) {
        delete _meshByID[id];
    }
    _meshByID.clear();
    _meshCache.clear();
    _ownedMeshIDs.clear();
    _nextMeshID = 1;

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

    for (uint32_t id : _ownedMeshIDs) delete _meshByID[id];
    _meshByID.clear();
    _meshCache.clear();
    _ownedMeshIDs.clear();
    _nextMeshID = 1;

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
        Console::Get()->Warn(fmt::format("AssetManager::LoadImage: Failed to read file bytes via FileSystem at '{}'", path));
        return nullptr;
    }

    int width, height, numChannels;
    if (!stbi_info_from_memory(fileData.data(), (int)fileData.size(), &width, &height, &numChannels)) {
        Console::Get()->Warn(fmt::format("stbi_info_from_memory: Failed to read image metadata at '{}'", path));
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

    stbi_set_flip_vertically_on_load(true);
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
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
                  {
                    "terrain",
                    { .vert = "assets/shaders/terrain_simple.vert",
                      .frag = "assets/shaders/terrain.frag" },
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

    auto* shader = new GLShaderProgram(props);
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
            Console::Get()->Warn(fmt::format("Failed to load texture at '{}', using default fallback texture.", regularPaths[j]));
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

TextureHandle AssetManager::CreateTexture(const std::string& path) {
    std::string redirectedPath = path;
    if (redirectedPath.size() >= 2 && redirectedPath[0] == '.' && redirectedPath[1] == '/')
        redirectedPath = redirectedPath.substr(2);
#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
    redirectedPath = RedirectToKTX2(redirectedPath); // also normalizes "./" internally
#endif

    // Return cached texture (TextureHandle) if already uploaded.
    auto it = _textureCache.find(redirectedPath);
    if (it != _textureCache.end()) {
        return TextureHandle(it->second.glID);
    }

#ifdef AE_USE_BASIS_UNIVERSAL
    // Route .ktx2 files to the GPU-compressed loader.
    if (redirectedPath.size() >= 5 && redirectedPath.compare(redirectedPath.size() - 5, 5, ".ktx2") == 0) {
        Texture2D tex2d;
        GLuint texID = LoadKTX2Texture(redirectedPath, &tex2d);
        textures.push_back(texID);
        _textureCache[redirectedPath] = tex2d;
        return TextureHandle(texID);
    }
#endif

    // Regular image (PNG / JPG / etc.) via stb_image.
    auto image = LoadImage(redirectedPath);
    if (!image) {
        Console::Get()->Warn(fmt::format("AssetManager::CreateTexture: Failed to load image at '{}', using default fallback texture.", redirectedPath));
        GLuint fallbackTex = defaultTextures.empty() ? 0u : defaultTextures[0];
        _textureCache[redirectedPath] = { fallbackTex, 0, 0, 0 };
        return TextureHandle(fallbackTex);
    }
    return CreateTextureFromImage(image);
}

TextureHandle AssetManager::CreateTextureFromImage(const std::shared_ptr<Image>& image) {
    if (!image) {
        Console::Get()->Warn("AssetManager::CreateTextureFromImage: Null image, returning default fallback texture.");
        GLuint fallbackTex = defaultTextures.empty() ? 0u : defaultTextures[0];
        return TextureHandle(fallbackTex);
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
    return TextureHandle(texID);
}

TextureHandle AssetManager::GetTexture(const std::string& name) const {
    auto it = _textureCache.find(name);
    if (it != _textureCache.end()) {
        return TextureHandle(it->second.glID);
    }
    return TextureHandle(); // Return invalid handle (id = INVALID)
}

GLuint AssetManager::GetTextureByID(uint32_t id) const {
    if (id < textures.size()) {
        return textures[id];
    }
    return 0;
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

MeshHandle AssetManager::CreateMesh(Mesh* mesh) {
    return CreateMesh("unnamed_" + std::to_string(_nextMeshID), mesh);
}

MeshHandle AssetManager::CreateMesh(const std::string& name, Mesh* mesh) {
    if (!mesh) {
        mesh = new Mesh();
    }
    uint32_t id = _nextMeshID++;
    _meshByID[id] = mesh;
    _meshCache[name] = id;
    _ownedMeshIDs.insert(id);
    return MeshHandle(id);
}

MeshHandle AssetManager::RegisterMesh(Mesh* mesh) {
    if (!mesh) return MeshHandle{};
    uint32_t id = _nextMeshID++;
    _meshByID[id] = mesh;
    return MeshHandle(id);
}

void AssetManager::UnregisterMesh(MeshHandle handle) {
    if (!handle.IsValid()) return;
    _meshByID.erase(handle.id);
    _ownedMeshIDs.erase(handle.id);
}

MeshHandle AssetManager::CreateCubeMesh(const std::string& name, float size) {
    auto mesh = MeshBuilder::CreateCube(size);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    return CreateMesh(name, mesh);
}

MeshHandle AssetManager::CreatePlaneMesh(const std::string& name, float width, float height) {
    auto mesh = MeshBuilder::CreatePlane(width, height);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    return CreateMesh(name, mesh);
}

MeshHandle AssetManager::CreatePlaneMeshSubdivided(const std::string& name,
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
    return CreateMesh(name, mesh);
}

MeshHandle AssetManager::CreateSphereMesh(const std::string& name, float radius, int division) {
    auto mesh = MeshBuilder::CreateSphere(radius, division);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    return CreateMesh(name, mesh);
}

MeshHandle AssetManager::CreateCapsuleMesh(const std::string& name, float radius, float height) {
    // TODO: Implement capsule mesh generation
    ENGINE_LOG("Capsule mesh '{}' created (generation not yet implemented)", name);
    auto mesh = new Mesh();
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    return CreateMesh(name, mesh);
}

MeshHandle AssetManager::CreateTerrainMesh(const std::string& name, float worldSize, int resolution) {
    auto mesh = MeshBuilder::CreateTerrain(worldSize, resolution);
    if (_materialCache.find("Default") != _materialCache.end()) {
        mesh->SetMaterial(GetMaterial("Default"));
    }
    return CreateMesh(name, mesh);
}

MeshHandle AssetManager::GetMesh(const std::string& name) const {
    auto it = _meshCache.find(name);
    if (it != _meshCache.end()) {
        return MeshHandle(it->second);
    }
    throw std::runtime_error(fmt::format("Mesh '{}' not found", name));
}

Mesh* AssetManager::GetMeshPtr(MeshHandle handle) const {
    if (!handle.IsValid()) return nullptr;
    auto it = _meshByID.find(handle.id);
    return it != _meshByID.end() ? it->second : nullptr;
}

std::shared_ptr<Mesh> AssetManager::LoadOBJ(const std::string& path) {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    auto mesh = std::make_shared<Mesh>(MeshType::PRIM);
    mesh->Initialize(vertices, indices);

    return mesh;  // LoadOBJ is unimplemented; returns unregistered mesh
}

MeshHandle AssetManager::LoadGLTF(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Custom image loader: decode embedded image bytes with explicit flip for OpenGL UV origin.
    // This runs during LoadASCIIFromFile / LoadBinaryFromFile before we get control back.
    loader.SetImageLoader(
        [](tinygltf::Image* img, const int /*imageIdx*/,
           std::string* /*err*/, std::string* /*warn*/,
           int /*reqWidth*/, int /*reqHeight*/,
           const unsigned char* bytes, int size, void* /*userdata*/) -> bool {
            int w, h, c;
            stbi_set_flip_vertically_on_load(true);
            unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &c, 4);
            stbi_set_flip_vertically_on_load(false);
            if (!data) return false;
            img->width     = w;
            img->height    = h;
            img->component = 4;
            img->image.assign(data, data + w * h * 4);
            stbi_image_free(data);
            return true;
        },
        nullptr
    );

    bool result;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".glb") == 0) {
        result = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        result = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }

    if (!warn.empty()) Console::Get()->Warn(fmt::format("LoadGLTF '{}': {}", path, warn));
    if (!err.empty())  Console::Get()->Warn(fmt::format("LoadGLTF '{}' error: {}", path, err));
    if (!result || model.meshes.empty()) return MeshHandle{};

    // Upload each referenced image to GPU immediately; CPU copy is discarded afterwards.
    // texHandles[i] corresponds to model.textures[i].
    std::vector<TextureHandle> texHandles;
    texHandles.reserve(model.textures.size());
    for (const auto& tex : model.textures) {
        if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size())) {
            texHandles.emplace_back();
            continue;
        }
        const auto& img = model.images[tex.source];
        if (!img.image.empty()) {
            auto cpuImg = std::make_shared<Image>(
                img.width, img.height, img.component,
                const_cast<unsigned char*>(img.image.data())
            );
            texHandles.push_back(CreateTextureFromImage(cpuImg));
        } else if (!img.uri.empty()) {
            texHandles.push_back(CreateTexture(img.uri));
        } else {
            texHandles.emplace_back();
        }
    }

    // Build a Material from a GLTF material index. Only the first material
    // encountered is assigned to the returned Mesh; multi-material support
    // requires a Scene-level concept (future work).
    auto buildMaterial = [&](int matIdx) -> Material* {
        if (matIdx < 0 || matIdx >= static_cast<int>(model.materials.size())) return nullptr;
        const auto& mat = model.materials[matIdx];
        auto texAt = [&](int texIdx) -> TextureHandle {
            return (texIdx >= 0 && texIdx < static_cast<int>(texHandles.size()))
                       ? texHandles[texIdx]
                       : TextureHandle{};
        };
        MaterialProps props;
        props.baseMap      = texAt(mat.pbrMetallicRoughness.baseColorTexture.index);
        props.normalMap    = texAt(mat.normalTexture.index);
        props.aoMap        = texAt(mat.occlusionTexture.index);
        props.roughnessMap = texAt(mat.pbrMetallicRoughness.metallicRoughnessTexture.index);
        props.metallicMap  = texAt(mat.pbrMetallicRoughness.metallicRoughnessTexture.index);
        const std::string matName = mat.name.empty() ? "gltf_mat" : mat.name;
        return CreateMaterial(matName, props);
    };

    // ── Accessor helpers with correct byteStride support ─────────────────────
    // GLTF allows interleaved vertex buffers: multiple attributes share one
    // bufferView with byteStride = total vertex size. A stride of 0 means
    // tightly packed (stride == component size * component count).

    auto readFloat3 = [&](const tinygltf::Accessor& acc) {
        const auto& bv  = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const size_t stride = bv.byteStride ? bv.byteStride : sizeof(float) * 3;
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        std::vector<glm::vec3> out(acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            const auto* v = reinterpret_cast<const float*>(base + i * stride);
            out[i] = { v[0], v[1], v[2] };
        }
        return out;
    };

    auto readFloat4 = [&](const tinygltf::Accessor& acc) {
        const auto& bv  = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const size_t stride = bv.byteStride ? bv.byteStride : sizeof(float) * 4;
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        std::vector<glm::vec4> out(acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            const auto* v = reinterpret_cast<const float*>(base + i * stride);
            out[i] = { v[0], v[1], v[2], v[3] };
        }
        return out;
    };

    // TEXCOORD may be FLOAT, UNSIGNED_BYTE normalized, or UNSIGNED_SHORT normalized per GLTF spec.
    auto readTexcoord = [&](const tinygltf::Accessor& acc) {
        const auto& bv  = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        std::vector<glm::vec2> out(acc.count);
        switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            const size_t stride = bv.byteStride ? bv.byteStride : sizeof(float) * 2;
            for (size_t i = 0; i < acc.count; ++i) {
                const auto* v = reinterpret_cast<const float*>(base + i * stride);
                out[i] = { v[0], v[1] };
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const size_t stride = bv.byteStride ? bv.byteStride : 2u;
            for (size_t i = 0; i < acc.count; ++i) {
                const uint8_t* v = base + i * stride;
                out[i] = { v[0] / 255.0f, v[1] / 255.0f };
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const size_t stride = bv.byteStride ? bv.byteStride : sizeof(uint16_t) * 2;
            for (size_t i = 0; i < acc.count; ++i) {
                const auto* v = reinterpret_cast<const uint16_t*>(base + i * stride);
                out[i] = { v[0] / 65535.0f, v[1] / 65535.0f };
            }
            break;
        }
        default:
            Console::Get()->Warn(fmt::format("LoadGLTF: unsupported TEXCOORD component type {}", acc.componentType));
            break;
        }
        return out;
    };

    // ── Concatenate all primitives from all meshes into a single Mesh ─────────
    // This flattening approach means one GPU buffer upload and no per-primitive
    // draw call overhead. Multi-material rendering is a Scene-level concern.
    std::vector<Vertex>   allVerts;
    std::vector<uint16_t> allIndices;
    Material* material = nullptr;

    for (const auto& srcMesh : model.meshes) {
        for (const auto& prim : srcMesh.primitives) {
            if (!prim.attributes.count("POSITION")) continue;

            const size_t vertBase  = allVerts.size();
            const auto&  posAcc    = model.accessors[prim.attributes.at("POSITION")];
            const size_t vertCount = posAcc.count;

            if (vertBase + vertCount > 65535) {
                Console::Get()->Warn(fmt::format(
                    "LoadGLTF '{}': vertex count exceeds uint16_t limit, primitive skipped. "
                    "Consider splitting the mesh or upgrading to 32-bit indices.",
                    path
                ));
                continue;
            }

            auto positions = readFloat3(posAcc);

            std::vector<glm::vec3> normals(vertCount, glm::vec3(0.f, 1.f, 0.f));
            if (prim.attributes.count("NORMAL"))
                normals = readFloat3(model.accessors[prim.attributes.at("NORMAL")]);

            std::vector<glm::vec2> uvs(vertCount, glm::vec2(0.f));
            if (prim.attributes.count("TEXCOORD_0"))
                uvs = readTexcoord(model.accessors[prim.attributes.at("TEXCOORD_0")]);

            // GLTF TANGENT is vec4: xyz = tangent direction, w = bitangent handedness.
            std::vector<glm::vec4> tangents4(vertCount, glm::vec4(1.f, 0.f, 0.f, 1.f));
            if (prim.attributes.count("TANGENT"))
                tangents4 = readFloat4(model.accessors[prim.attributes.at("TANGENT")]);

            for (size_t i = 0; i < vertCount; ++i) {
                const glm::vec3 t = glm::vec3(tangents4[i]);
                const glm::vec3 b = glm::cross(normals[i], t) * tangents4[i].w;
                allVerts.push_back({ positions[i], uvs[i], normals[i], t, b });
            }

            // Index buffer: GLTF scalar accessors are always tightly packed (byteStride == 0).
            if (prim.indices >= 0) {
                const auto&    idxAcc = model.accessors[prim.indices];
                const auto&    bv     = model.bufferViews[idxAcc.bufferView];
                const auto&    buf    = model.buffers[bv.buffer];
                const uint8_t* base   = buf.data.data() + bv.byteOffset + idxAcc.byteOffset;

                switch (idxAcc.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    for (size_t i = 0; i < idxAcc.count; ++i) {
                        uint32_t idx;
                        std::memcpy(&idx, base + i * sizeof(uint32_t), sizeof(uint32_t));
                        allIndices.push_back(static_cast<uint16_t>(vertBase + idx));
                    }
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    for (size_t i = 0; i < idxAcc.count; ++i) {
                        uint16_t idx;
                        std::memcpy(&idx, base + i * sizeof(uint16_t), sizeof(uint16_t));
                        allIndices.push_back(static_cast<uint16_t>(vertBase + idx));
                    }
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    for (size_t i = 0; i < idxAcc.count; ++i)
                        allIndices.push_back(static_cast<uint16_t>(vertBase + base[i]));
                    break;
                default:
                    Console::Get()->Warn(fmt::format("LoadGLTF: unsupported index component type {}", idxAcc.componentType));
                    break;
                }
            } else {
                for (size_t i = 0; i < vertCount; ++i)
                    allIndices.push_back(static_cast<uint16_t>(vertBase + i));
            }

            if (!material && prim.material >= 0)
                material = buildMaterial(prim.material);
        }
    }

    if (allVerts.empty()) {
        Console::Get()->Warn(fmt::format("LoadGLTF: no geometry found in '{}'", path));
        return MeshHandle{};
    }

    auto* mesh = new Mesh(MeshType::PRIM);
    mesh->Initialize(allVerts, allIndices);
    if (material) mesh->SetMaterial(material);

    ENGINE_LOG("LoadGLTF '{}': {} verts, {} indices", path, allVerts.size(), allIndices.size());
    return CreateMesh(path, mesh);
}

TextureHandle AssetManager::CreateHeightmapTexture(
    const std::string& name, const std::vector<float>& grid, int width, int height
) {
    auto it = _textureCache.find(name);
    if (it != _textureCache.end()) {
        return TextureHandle(it->second.glID);
    }

    std::vector<uint8_t> bytes(width * height);
    for (int i = 0; i < width * height; ++i)
        bytes[i] = static_cast<uint8_t>(std::clamp(grid[i] * 255.0f, 0.0f, 255.0f));

    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, bytes.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    textures.push_back(texID);
    _textureCache[name] = { texID, (uint32_t)width, (uint32_t)height, (size_t)(width * height) };
    return TextureHandle(texID);
}
