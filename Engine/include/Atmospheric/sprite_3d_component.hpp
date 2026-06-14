#pragma once
#include "canvas_drawable.hpp"
#include "component.hpp"
#include "globals.hpp"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <functional>
#include <memory>
#include <vector>

// ============================================================================
// Sprite Mesh - Grid of vertices for deformable sprites
// ============================================================================

struct SpriteMeshVertex {
    glm::vec2 position;// Local position (relative to sprite center)
    glm::vec2 uv;// Texture coordinate
    glm::vec4 color = glm::vec4(1.0f);// Per-vertex color (for gradients, etc.)
};

// SpriteMesh - Converts a sprite into a deformable vertex grid
// Usage:
//   sprite.EnableMesh(4, 4);  // 4x4 grid = 16 vertices
//   sprite.SetVertexOffset(5, {0.1f, 0.0f});  // Push vertex 5 to the right
class SpriteMesh {
public:
    SpriteMesh() = default;

    // Generate a grid mesh for the sprite
    // cols/rows define subdivision (more = smoother deformation, more vertices)
    void GenerateGrid(int cols, int rows, const glm::vec2& size);

    // Vertex access
    int GetVertexCount() const { return static_cast<int>(_vertices.size()); }
    int GetCols() const { return _cols; }
    int GetRows() const { return _rows; }

    // Get vertex by index
    SpriteMeshVertex& GetVertex(int index);
    const SpriteMeshVertex& GetVertex(int index) const;

    // Get vertex by grid position
    SpriteMeshVertex& GetVertex(int col, int row);
    const SpriteMeshVertex& GetVertex(int col, int row) const;

    // Offset operations (non-destructive, adds to base position)
    void SetVertexOffset(int index, const glm::vec2& offset);
    void SetVertexOffset(int col, int row, const glm::vec2& offset);
    glm::vec2 GetVertexOffset(int index) const;

    // Reset all offsets to zero
    void ResetOffsets();

    // Get final positions (base + offset)
    const std::vector<SpriteMeshVertex>& GetVertices() const { return _vertices; }
    const std::vector<glm::vec2>& GetOffsets() const { return _offsets; }
    const std::vector<uint32_t>& GetIndices() const { return _indices; }

    // Apply offsets to vertices (call before rendering)
    void ApplyOffsets();

private:
    std::vector<SpriteMeshVertex> _vertices;
    std::vector<SpriteMeshVertex> _baseVertices;// Original positions
    std::vector<glm::vec2> _offsets;
    std::vector<uint32_t> _indices;
    int _cols = 0;
    int _rows = 0;
};

// ============================================================================
// Deformers - Procedural vertex manipulation
// ============================================================================

class Sprite3DComponent;// Forward declaration

// Base class for all deformers
class SpriteDeformer {
public:
    virtual ~SpriteDeformer() = default;

    // Apply deformation to mesh vertices
    // deltaTime: for animated deformations
    // mesh: the sprite mesh to deform
    virtual void Apply(float deltaTime, SpriteMesh& mesh) = 0;

    // Enable/disable
    bool enabled = true;
    float weight = 1.0f;// Blend weight (0-1)
};

// Sine wave deformation
class SineDeformer : public SpriteDeformer {
public:
    glm::vec2 direction = glm::vec2(1.0f, 0.0f);// Wave direction
    float amplitude = 0.1f;// Wave height
    float frequency = 2.0f;// Waves per unit
    float speed = 1.0f;// Animation speed
    float phase = 0.0f;// Phase offset

    void Apply(float deltaTime, SpriteMesh& mesh) override;

private:
    float _time = 0.0f;
};

// Bulge/pinch deformation (radial)
class BulgeDeformer : public SpriteDeformer {
public:
    glm::vec2 center = glm::vec2(0.5f, 0.5f);// Normalized center (0-1)
    float radius = 0.5f;// Effect radius
    float strength = 0.2f;// Positive = bulge, negative = pinch

    void Apply(float deltaTime, SpriteMesh& mesh) override;
};

// Bend deformation (arc)
class BendDeformer : public SpriteDeformer {
public:
    float angle = 0.0f;// Bend angle in radians
    glm::vec2 pivot = glm::vec2(0.5f, 0.0f);// Pivot point (normalized)
    glm::vec2 axis = glm::vec2(0.0f, 1.0f);// Bend axis

    void Apply(float deltaTime, SpriteMesh& mesh) override;
};

// Twist deformation (rotate vertices based on distance from center)
class TwistDeformer : public SpriteDeformer {
public:
    glm::vec2 center = glm::vec2(0.5f, 0.5f);
    float angle = 0.0f;// Max twist angle
    float falloff = 1.0f;// How quickly twist diminishes from center

    void Apply(float deltaTime, SpriteMesh& mesh) override;
};

// Jiggle physics simulation (spring-based secondary motion)
class JiggleDeformer : public SpriteDeformer {
public:
    float stiffness = 0.5f;// Spring stiffness (0-1)
    float damping = 0.8f;// Velocity damping (0-1)
    float mass = 1.0f;// Affects inertia
    glm::vec2 gravity = glm::vec2(0.0f, -0.1f);// Per-frame gravity

    // Which vertices are affected (empty = all)
    std::vector<int> affectedVertices;

    // Which vertices are pinned (don't move)
    std::vector<int> pinnedVertices;

    void Apply(float deltaTime, SpriteMesh& mesh) override;
    void Reset();// Reset physics state

private:
    std::vector<glm::vec2> _velocities;
    std::vector<glm::vec2> _prevPositions;
    bool _initialized = false;

    void Initialize(const SpriteMesh& mesh);
};

// Custom deformer using user-provided function
class CustomDeformer : public SpriteDeformer {
public:
    using DeformFunc = std::function<void(float deltaTime, SpriteMesh& mesh)>;
    DeformFunc deformFunction;

    void Apply(float deltaTime, SpriteMesh& mesh) override {
        if (deformFunction) deformFunction(deltaTime, mesh);
    }
};

// ============================================================================
// Sprite3D Props
// ============================================================================

struct Sprite3DProps {
    glm::vec2 size = glm::vec2(1.0f, 1.0f);// Size in world units
    glm::vec2 pivot = glm::vec2(0.5f, 0.5f);// (0,0) = bottom-left, (1,1) = top-right
    glm::vec4 color = glm::vec4(1.0f);
    int textureID = -1;
    BillboardMode billboardMode = BillboardMode::ViewPlane;// Default: face camera (Y-locked)

    // Mesh subdivision for deformation (0 = no mesh, use simple quad)
    int meshCols = 0;
    int meshRows = 0;
};

// ============================================================================
// Sprite3DComponent - A sprite rendered in 3D world space
// ============================================================================

// Sprite3DComponent - A sprite rendered in 3D world space
// Used for health bars, name labels, damage numbers in 3D games
// Supports billboard modes to face the camera
// Supports mesh deformation for jiggle physics, waves, etc.
class Sprite3DComponent : public CanvasDrawable {
public:
    Sprite3DComponent(GameObject* gameObject, const Sprite3DProps& props);

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;

    void Draw(BatchRenderer2D* renderer) override;

    bool CanTick() const override {
        return HasMesh() && !_deformers.empty();// Tick if we have deformers
    }

    void Tick(float deltaTime);// Update deformers

    // ─── Basic Properties ───
    glm::vec2 GetSize() const { return _size; }
    glm::vec2 GetPivot() const { return _pivot; }
    glm::vec4 GetColor() const { return _color; }
    int GetTextureID() const { return _textureID; }
    BillboardMode GetBillboardMode() const { return _billboardMode; }

    Sprite3DComponent& SetSize(const glm::vec2& size);
    Sprite3DComponent& SetSize(float w, float h) { return SetSize(glm::vec2(w, h)); }
    Sprite3DComponent& SetPivot(const glm::vec2& pivot) { _pivot = pivot; return *this; }
    Sprite3DComponent& SetColor(const glm::vec4& color) { _color = color; return *this; }
    Sprite3DComponent& SetTextureID(int textureID) { _textureID = textureID; return *this; }
    Sprite3DComponent& SetBillboardMode(BillboardMode mode) { _billboardMode = mode; return *this; }

    // ─── UV for Spritesheets ───
    glm::vec2 GetUVMin() const { return _uvMin; }
    glm::vec2 GetUVMax() const { return _uvMax; }
    Sprite3DComponent& SetUVs(const glm::vec2& min, const glm::vec2& max) {
        _uvMin = min;
        _uvMax = max;
        if (_mesh) RegenerateMesh();
        return *this;
    }

    CanvasLayer GetLayer() const override {
        return CanvasLayer::LAYER_EFFECTS;  // Render after regular world objects
    }

    // ─── Mesh / Deformation API ───

    // Enable mesh mode with subdivision
    // More subdivisions = smoother deformation but more vertices
    Sprite3DComponent& EnableMesh(int cols, int rows);

    // Disable mesh mode (use simple quad)
    Sprite3DComponent& DisableMesh();

    // Check if mesh is enabled
    bool HasMesh() const { return _mesh != nullptr; }

    // Get mesh for direct manipulation
    SpriteMesh* GetMesh() { return _mesh.get(); }
    const SpriteMesh* GetMesh() const { return _mesh.get(); }

    // ─── Deformer Management ───

    // Add a deformer (takes ownership)
    template<typename T, typename... Args>
    T* AddDeformer(Args&&... args) {
        auto deformer = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = deformer.get();
        _deformers.push_back(std::move(deformer));
        return ptr;
    }

    // Get deformer by index
    SpriteDeformer* GetDeformer(int index);

    // Get deformer by type (returns first match)
    template<typename T>
    T* GetDeformer() {
        for (auto& d : _deformers) {
            if (auto* typed = dynamic_cast<T*>(d.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    // Remove all deformers
    void ClearDeformers() { _deformers.clear(); }

    // Get deformer count
    int GetDeformerCount() const { return static_cast<int>(_deformers.size()); }

private:
    // Basic properties
    glm::vec2 _size;
    glm::vec2 _pivot;
    glm::vec4 _color;
    int _textureID;
    BillboardMode _billboardMode;
    glm::vec2 _uvMin = glm::vec2(0.0f, 0.0f);
    glm::vec2 _uvMax = glm::vec2(1.0f, 1.0f);

    // Mesh for deformation
    std::unique_ptr<SpriteMesh> _mesh;
    std::vector<std::unique_ptr<SpriteDeformer>> _deformers;

    // Helpers
    glm::mat4 CalculateBillboardMatrix(const glm::vec3& position, const glm::vec3& cameraPosition) const;
    void RegenerateMesh();
    void DrawSimpleQuad(BatchRenderer2D* renderer, const glm::mat4& transform);
    void DrawMesh(BatchRenderer2D* renderer, const glm::mat4& transform);
};
