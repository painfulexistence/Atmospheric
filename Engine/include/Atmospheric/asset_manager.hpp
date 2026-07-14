#pragma once
#include "globals.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Mesh;
class Material;
class WaterMaterial;
class TerrainMaterial;
class GrassMaterial;
class RiverMaterial;
class VATMaterial;
class PortalMaterial;
class VoxelMaterial;
class ShaderProgram;
struct ShaderProgramProps;
struct MaterialProps;

class Image {
public:
    Image(int width, int height, int numChannels, unsigned char* data)
      : width(width), height(height), channelCount(numChannels),
        byteArray(std::vector<unsigned char>(data, data + width * height * numChannels)) {
    }

    int width;
    int height;
    int channelCount;
    std::vector<unsigned char> byteArray;
};


struct Texture2D {
    GLuint glID = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t bytes = 0;
};

// Owned by Application (see Application's service members); Get() is a
// non-owning locator into that instance. Construction order relative to the
// Window matters: the destructor releases GL textures, so Application declares
// its AssetManager member after _window (destroyed while the context lives).
class AssetManager {
public:
    AssetManager();
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    static AssetManager& Get();

    // ========== CPU Resource Management ==========
    std::shared_ptr<Image> LoadImage(const std::string& path);

    Material* CreateMaterial(const std::string& name, const MaterialProps& props);
    Material* CreateMaterial(const MaterialProps& props);
    WaterMaterial* CreateWaterMaterial();
    PortalMaterial* CreatePortalMaterial();
    TerrainMaterial* CreateTerrainMaterial();
    GrassMaterial* CreateGrassMaterial();
    RiverMaterial* CreateRiverMaterial();
    VATMaterial* CreateVATMaterial();
    VoxelMaterial* CreateVoxelMaterial();
    Material* GetMaterial(const std::string& name) const;
    Material* GetMaterialByID(uint32_t id) const;
    // Handle-based access: handles are stable references that survive scene
    // unloads (they resolve to nullptr instead of dangling).
    [[nodiscard]] MaterialHandle GetMaterialHandle(const std::string& name) const;// INVALID if absent
    [[nodiscard]] MaterialHandle GetMaterialHandle(const Material* material) const;// INVALID if absent
    [[nodiscard]] Material* ResolveMaterial(MaterialHandle handle) const;// nullptr if invalid or unloaded
    void LoadMaterials(const std::vector<MaterialProps>& materialDefs);

    ShaderProgram* CreateShader(const std::string& name, const ShaderProgramProps& props);
    ShaderProgram* GetShader(const std::string& name) const;
    ShaderProgram* GetShaderByID(uint32_t id) const;
    // Handle-based access for references held across frames: handles survive
    // scene unloads (they resolve to nullptr instead of dangling). Transient
    // per-pass lookups can keep using GetShader.
    [[nodiscard]] ShaderHandle GetShaderHandle(const std::string& name) const;// INVALID if absent
    [[nodiscard]] ShaderProgram* ResolveShader(ShaderHandle handle) const;// nullptr if invalid or unloaded
    void LoadDefaultShaders();
    void LoadShaders(const std::unordered_map<std::string, ShaderProgramProps>& shaderDefs);
    void ReloadShaders();

    // ========== GPU Resource Management ==========
    TextureHandle CreateTexture(const std::string& path);
    TextureHandle CreateTextureFromImage(const std::shared_ptr<Image>& image);
    TextureHandle GetTexture(const std::string& name) const;
    GLuint GetTextureByID(uint32_t id) const;
    std::string GetTexturePath(GLuint id) const;
    void LoadDefaultTextures();
    void LoadTextures(const std::vector<std::string>& paths);
    MeshHandle CreateMesh(Mesh* mesh = nullptr);
    MeshHandle CreateMesh(const std::string& name, Mesh* mesh = nullptr);
    MeshHandle CreateCubeMesh(const std::string& name, float size = 1.0f);
    MeshHandle CreatePlaneMesh(const std::string& name, float width, float height);
    MeshHandle CreatePlaneMeshSubdivided(const std::string& name, float width, float height, int subdivisions);
    MeshHandle CreateDiscMesh(const std::string& name, float radius, int segments = 48);
    MeshHandle CreateSphereMesh(const std::string& name, float radius = 0.5f, int division = 18);
    MeshHandle CreateCapsuleMesh(const std::string& name, float radius = 0.5f, float height = 3.0f);
    MeshHandle CreateTerrainMesh(const std::string& name, float worldSize = 1024.f, int resolution = 10);
    MeshHandle GetMesh(const std::string& name) const;
    Mesh* GetMeshPtr(MeshHandle handle) const;
    // Registers an externally-owned mesh (e.g. a unique_ptr<Mesh> held by a
    // voxel chunk) so it gets a handle usable in RenderCommand/queue sorting.
    // AssetManager does NOT take ownership; call UnregisterMesh before the
    // owner destroys the mesh.
    MeshHandle RegisterMesh(Mesh* mesh);
    void UnregisterMesh(MeshHandle handle);
    // Upload a normalized [0,1] float grid as a GL_R8 grayscale texture.
    // Returns the scene-texture index usable as Material::heightMap.
    TextureHandle
        CreateHeightmapTexture(const std::string& name, const std::vector<float>& grid, int width, int height);
    // Re-upload the pixel data of a heightmap texture previously created with
    // CreateHeightmapTexture (e.g. after regenerating a NoiseHeightField).
    void UpdateHeightmapTexture(TextureHandle handle, const std::vector<float>& grid, int width, int height);
    // Create (or re-upload, when a texture with this name already exists) a
    // tightly-packed RGBA8 texture from raw pixels. Used by TerrainStreamer to
    // recycle per-tile splat-map textures without growing the texture cache.
    // tiled=true sets REPEAT wrap and builds mipmaps (trilinear min filter) —
    // required for high-frequency tiled detail layers, which shimmer badly
    // when sampled without mips. Default (clamp, no mips) suits per-tile maps
    // like splats that are sampled at ~1:1.
    TextureHandle CreateOrUpdateTextureRGBA8(
        const std::string& name, const unsigned char* data, int width, int height, bool tiled = false
    );
    std::shared_ptr<Mesh> LoadOBJ(const std::string& path);
    MeshHandle LoadGLTF(const std::string& path);
    // Load an equirectangular HDR (.hdr) as a filterable RGBA16F texture for use
    // as a skybox / IBL environment map. Returns an invalid handle on failure.
    // Native path only for now (reads from disk via stb_image's float loader).
    TextureHandle LoadHDR(const std::string& path);

    // ========== Resource Access ==========
    const std::vector<GLuint>& GetTextures() const {
        return textures;
    }
    const std::vector<GLuint>& GetDefaultTextures() const {
        return defaultTextures;
    }
    const std::vector<std::unique_ptr<Material>>& GetMaterials() const {
        return materials;
    }

    size_t getTotalTextureBytes() const;

    // ========== Component-owned asset cleanup ==========
    // Null the slot in the flat vector, erase from the name cache, and delete
    // the object.  Index stability is preserved (no vector compaction).
    void RemoveMaterial(Material* mat);
    void RemoveTexture(const std::string& path);

    // ========== Per-scene asset ownership ==========
    // Store the raw scene JSON so UnloadSceneAssets can re-parse it to free
    // every asset type declared by that scene.  Adding new asset types to the
    // JSON format (fonts, sounds, ui, …) is automatically handled — no struct
    // changes needed here.
    //
    // Invariant: correct when simultaneously-loaded scenes do not share asset
    // paths.  Namespace each demo's assets under its own subdirectory.
    void StoreSceneJson(const std::string& sceneName, const std::string& json);
    void UnloadSceneAssets(const std::string& sceneName);

    // ========== Cleanup ==========
    void Clear();
    void ClearSceneAssets();// Clears scene assets only, preserving defaults.

private:
    static AssetManager* instance;

    // Images
    std::unordered_map<std::string, std::shared_ptr<Image>> _imageCache;

    // Shaders
    std::vector<std::unique_ptr<ShaderProgram>> shaders;
    std::unordered_map<std::string, uint32_t> _shaderCache;
    uint32_t _nextShaderID = 0;
    uint32_t _defaultShaderCount = 0;

    // Materials
    std::vector<std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, uint32_t> _materialCache;
    uint32_t _nextMaterialID = 0;

    // Textures
    std::vector<GLuint> defaultTextures;
    std::vector<GLuint> textures;
    std::unordered_map<std::string, Texture2D> _textureCache;
    uint32_t _nextTextureID = 0;

    // Meshes
    std::unordered_map<uint32_t, Mesh*> _meshByID;
    std::unordered_map<std::string, uint32_t> _meshCache;
    std::unordered_set<uint32_t> _ownedMeshIDs;// IDs AssetManager must delete; excludes RegisterMesh entries
    uint32_t _nextMeshID = 1;

    // Raw JSON strings keyed by scene name — re-parsed on unload.
    std::unordered_map<std::string, std::string> _sceneJsons;

#ifdef AE_USE_BASIS_UNIVERSAL
    // KTX2 / Basis Universal GPU-compressed texture loader.
    // Transcodes BasisLZ / UASTC data to:
    //   - ETC2  on Emscripten/WebGL2 (guaranteed by GLES3 spec)
    //   - S3TC  on desktop OpenGL (checked at runtime; falls back to ETC2)
    // On web: bytes are sourced from FileSystem::Get().ConsumeSync(path),
    //         which consumes the in-process cache populated by Prefetch().
    // On native: bytes are read directly from disk if not cached.
    // Returns the GL texture object ID.
    GLuint LoadKTX2Texture(const std::string& path, Texture2D* out = nullptr);

    // True after basist::basisu_transcoder_init() has been called.
    bool _basisuInitialized = false;
#endif
};