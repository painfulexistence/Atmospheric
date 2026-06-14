#include "sprite_3d_component.hpp"
#include "application.hpp"
#include "batch_renderer_2d.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include <cmath>

// ============================================================================
// SpriteMesh Implementation
// ============================================================================

void SpriteMesh::GenerateGrid(int cols, int rows, const glm::vec2& size) {
    _cols = cols;
    _rows = rows;
    _vertices.clear();
    _baseVertices.clear();
    _offsets.clear();
    _indices.clear();

    // Generate vertices
    for (int y = 0; y <= rows; ++y) {
        for (int x = 0; x <= cols; ++x) {
            SpriteMeshVertex vertex;

            // Position: centered, from -0.5 to 0.5
            float px = (static_cast<float>(x) / cols) - 0.5f;
            float py = (static_cast<float>(y) / rows) - 0.5f;
            vertex.position = glm::vec2(px, py);

            // UV
            vertex.uv = glm::vec2(
                static_cast<float>(x) / cols,
                static_cast<float>(y) / rows
            );

            vertex.color = glm::vec4(1.0f);

            _vertices.push_back(vertex);
            _baseVertices.push_back(vertex);
            _offsets.push_back(glm::vec2(0.0f));
        }
    }

    // Generate indices (two triangles per cell)
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            int topLeft = y * (cols + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (y + 1) * (cols + 1) + x;
            int bottomRight = bottomLeft + 1;

            // First triangle
            _indices.push_back(topLeft);
            _indices.push_back(bottomLeft);
            _indices.push_back(topRight);

            // Second triangle
            _indices.push_back(topRight);
            _indices.push_back(bottomLeft);
            _indices.push_back(bottomRight);
        }
    }
}

SpriteMeshVertex& SpriteMesh::GetVertex(int index) {
    return _vertices[index];
}

const SpriteMeshVertex& SpriteMesh::GetVertex(int index) const {
    return _vertices[index];
}

SpriteMeshVertex& SpriteMesh::GetVertex(int col, int row) {
    return _vertices[row * (_cols + 1) + col];
}

const SpriteMeshVertex& SpriteMesh::GetVertex(int col, int row) const {
    return _vertices[row * (_cols + 1) + col];
}

void SpriteMesh::SetVertexOffset(int index, const glm::vec2& offset) {
    if (index >= 0 && index < static_cast<int>(_offsets.size())) {
        _offsets[index] = offset;
    }
}

void SpriteMesh::SetVertexOffset(int col, int row, const glm::vec2& offset) {
    int index = row * (_cols + 1) + col;
    SetVertexOffset(index, offset);
}

glm::vec2 SpriteMesh::GetVertexOffset(int index) const {
    if (index >= 0 && index < static_cast<int>(_offsets.size())) {
        return _offsets[index];
    }
    return glm::vec2(0.0f);
}

void SpriteMesh::ResetOffsets() {
    for (auto& offset : _offsets) {
        offset = glm::vec2(0.0f);
    }
}

void SpriteMesh::ApplyOffsets() {
    for (size_t i = 0; i < _vertices.size(); ++i) {
        _vertices[i].position = _baseVertices[i].position + _offsets[i];
    }
}

// ============================================================================
// Deformer Implementations
// ============================================================================

void SineDeformer::Apply(float deltaTime, SpriteMesh& mesh) {
    if (!enabled) return;

    _time += deltaTime * speed;

    glm::vec2 perpendicular(-direction.y, direction.x);

    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        const auto& base = mesh.GetVertex(i);
        float distance = glm::dot(base.position + glm::vec2(0.5f), direction);
        float wave = std::sin(distance * frequency * 6.28318f + _time + phase);
        glm::vec2 offset = perpendicular * wave * amplitude * weight;
        mesh.SetVertexOffset(i, mesh.GetVertexOffset(i) + offset);
    }
}

void BulgeDeformer::Apply(float deltaTime, SpriteMesh& mesh) {
    if (!enabled) return;

    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        const auto& base = mesh.GetVertex(i);
        // Normalize position to 0-1 range
        glm::vec2 normalizedPos = base.position + glm::vec2(0.5f);

        glm::vec2 toCenter = normalizedPos - center;
        float distance = glm::length(toCenter);

        if (distance < radius && distance > 0.001f) {
            float falloff = 1.0f - (distance / radius);
            falloff = falloff * falloff;// Quadratic falloff
            glm::vec2 offset = glm::normalize(toCenter) * strength * falloff * weight;
            mesh.SetVertexOffset(i, mesh.GetVertexOffset(i) + offset);
        }
    }
}

void BendDeformer::Apply(float deltaTime, SpriteMesh& mesh) {
    if (!enabled || std::abs(angle) < 0.001f) return;

    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        const auto& base = mesh.GetVertex(i);
        glm::vec2 normalizedPos = base.position + glm::vec2(0.5f);

        // Distance along bend axis from pivot
        float t = glm::dot(normalizedPos - pivot, axis);

        // Calculate bend
        float bendAngle = t * angle * weight;
        float cosA = std::cos(bendAngle);
        float sinA = std::sin(bendAngle);

        // Perpendicular to axis
        glm::vec2 perp(-axis.y, axis.x);

        // Rotate around pivot
        glm::vec2 relative = normalizedPos - pivot;
        glm::vec2 rotated;
        rotated.x = relative.x * cosA - relative.y * sinA;
        rotated.y = relative.x * sinA + relative.y * cosA;

        glm::vec2 newPos = pivot + rotated - glm::vec2(0.5f);
        mesh.SetVertexOffset(i, mesh.GetVertexOffset(i) + (newPos - base.position));
    }
}

void TwistDeformer::Apply(float deltaTime, SpriteMesh& mesh) {
    if (!enabled || std::abs(angle) < 0.001f) return;

    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        const auto& base = mesh.GetVertex(i);
        glm::vec2 normalizedPos = base.position + glm::vec2(0.5f);

        glm::vec2 toCenter = normalizedPos - center;
        float distance = glm::length(toCenter);

        // More twist at edges
        float twistAmount = distance * angle * weight / falloff;

        float cosA = std::cos(twistAmount);
        float sinA = std::sin(twistAmount);

        glm::vec2 rotated;
        rotated.x = toCenter.x * cosA - toCenter.y * sinA;
        rotated.y = toCenter.x * sinA + toCenter.y * cosA;

        glm::vec2 newPos = center + rotated - glm::vec2(0.5f);
        mesh.SetVertexOffset(i, mesh.GetVertexOffset(i) + (newPos - base.position));
    }
}

void JiggleDeformer::Initialize(const SpriteMesh& mesh) {
    int count = mesh.GetVertexCount();
    _velocities.resize(count, glm::vec2(0.0f));
    _prevPositions.resize(count);

    for (int i = 0; i < count; ++i) {
        _prevPositions[i] = mesh.GetVertex(i).position;
    }

    _initialized = true;
}

void JiggleDeformer::Reset() {
    _velocities.clear();
    _prevPositions.clear();
    _initialized = false;
}

void JiggleDeformer::Apply(float deltaTime, SpriteMesh& mesh) {
    if (!enabled) return;

    if (!_initialized) {
        Initialize(mesh);
    }

    // Check if vertex is pinned
    auto isPinned = [this](int index) {
        for (int p : pinnedVertices) {
            if (p == index) return true;
        }
        return false;
    };

    // Check if vertex is affected
    auto isAffected = [this](int index) {
        if (affectedVertices.empty()) return true;// All affected if list empty
        for (int a : affectedVertices) {
            if (a == index) return true;
        }
        return false;
    };

    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        if (isPinned(i) || !isAffected(i)) continue;

        const auto& base = mesh.GetVertex(i);
        glm::vec2 currentPos = base.position + mesh.GetVertexOffset(i);
        glm::vec2 targetPos = base.position;// Rest position

        // Spring force towards rest position
        glm::vec2 springForce = (targetPos - currentPos) * stiffness;

        // Apply gravity
        _velocities[i] += gravity * mass;

        // Apply spring
        _velocities[i] += springForce;

        // Apply damping
        _velocities[i] *= damping;

        // Update position
        glm::vec2 newPos = currentPos + _velocities[i] * deltaTime * weight;

        mesh.SetVertexOffset(i, newPos - base.position);
    }
}

// ============================================================================
// Sprite3DComponent Implementation
// ============================================================================

Sprite3DComponent::Sprite3DComponent(GameObject* gameObject, const Sprite3DProps& props)
    : CanvasDrawable(gameObject) {
    _size = props.size;
    _color = props.color;
    _pivot = props.pivot;
    _textureID = props.textureID;
    _billboardMode = props.billboardMode;

    if (props.meshCols > 0 && props.meshRows > 0) {
        EnableMesh(props.meshCols, props.meshRows);
    }
}

std::string Sprite3DComponent::GetName() const {
    return std::string("Sprite3DComponent");
}

void Sprite3DComponent::OnAttach() {
    gameObject->GetApp()->GetGraphicsServer()->RegisterCanvasDrawable(this);
}

void Sprite3DComponent::OnDetach() {
}

Sprite3DComponent& Sprite3DComponent::SetSize(const glm::vec2& size) {
    _size = size;
    if (_mesh) {
        RegenerateMesh();
    }
    return *this;
}

Sprite3DComponent& Sprite3DComponent::EnableMesh(int cols, int rows) {
    _mesh = std::make_unique<SpriteMesh>();
    _mesh->GenerateGrid(cols, rows, _size);
    return *this;
}

Sprite3DComponent& Sprite3DComponent::DisableMesh() {
    _mesh.reset();
    _deformers.clear();
    return *this;
}

void Sprite3DComponent::RegenerateMesh() {
    if (_mesh) {
        int cols = _mesh->GetCols();
        int rows = _mesh->GetRows();
        _mesh->GenerateGrid(cols, rows, _size);
    }
}

SpriteDeformer* Sprite3DComponent::GetDeformer(int index) {
    if (index >= 0 && index < static_cast<int>(_deformers.size())) {
        return _deformers[index].get();
    }
    return nullptr;
}

void Sprite3DComponent::Tick(float deltaTime) {
    if (!_mesh || _deformers.empty()) return;

    // Reset offsets
    _mesh->ResetOffsets();

    // Apply each deformer in order
    for (auto& deformer : _deformers) {
        if (deformer->enabled) {
            deformer->Apply(deltaTime, *_mesh);
        }
    }

    // Apply final offsets
    _mesh->ApplyOffsets();
}

glm::mat4 Sprite3DComponent::CalculateBillboardMatrix(const glm::vec3& position, const glm::vec3& cameraPosition) const {
    glm::mat4 billboard = glm::mat4(1.0f);

    switch (_billboardMode) {
    case BillboardMode::ViewPoint: {
        glm::vec3 look = glm::normalize(cameraPosition - position);
        glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

        if (glm::abs(glm::dot(look, worldUp)) > 0.999f) {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        glm::vec3 right = glm::normalize(glm::cross(worldUp, look));
        glm::vec3 up = glm::cross(look, right);

        billboard[0] = glm::vec4(right, 0.0f);
        billboard[1] = glm::vec4(up, 0.0f);
        billboard[2] = glm::vec4(look, 0.0f);
        billboard[3] = glm::vec4(position, 1.0f);
        break;
    }
    case BillboardMode::ViewPlane: {
        glm::vec3 look = cameraPosition - position;
        look.y = 0.0f;

        if (glm::length(look) < 0.001f) {
            look = glm::vec3(0.0f, 0.0f, 1.0f);
        } else {
            look = glm::normalize(look);
        }

        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(up, look));

        billboard[0] = glm::vec4(right, 0.0f);
        billboard[1] = glm::vec4(up, 0.0f);
        billboard[2] = glm::vec4(look, 0.0f);
        billboard[3] = glm::vec4(position, 1.0f);
        break;
    }
    case BillboardMode::None:
    default:
        billboard = glm::translate(glm::mat4(1.0f), position);
        break;
    }

    return billboard;
}

void Sprite3DComponent::DrawSimpleQuad(BatchRenderer2D* renderer, const glm::mat4& transform) {
    glm::vec2 uvs[4] = {
        { _uvMin.x, _uvMin.y },
        { _uvMax.x, _uvMin.y },
        { _uvMax.x, _uvMax.y },
        { _uvMin.x, _uvMax.y }
    };
    renderer->DrawQuad(transform, _textureID, uvs, _color);
}

void Sprite3DComponent::DrawMesh(BatchRenderer2D* renderer, const glm::mat4& transform) {
    // Convert SpriteMesh to BatchVertex format
    const auto& meshVertices = _mesh->GetVertices();
    const auto& meshIndices = _mesh->GetIndices();

    std::vector<BatchVertex> batchVertices;
    batchVertices.reserve(meshVertices.size());

    // UV interpolation
    glm::vec2 uvRange = _uvMax - _uvMin;

    for (const auto& v : meshVertices) {
        BatchVertex bv;
        // Transform position (mesh is in -0.5 to 0.5 range, scale already applied in transform)
        bv.position = glm::vec3(transform * glm::vec4(v.position.x, v.position.y, 0.0f, 1.0f));
        bv.color = _color * v.color;
        // Interpolate UVs based on mesh UV and sprite UV range
        bv.uv = _uvMin + v.uv * uvRange;
        bv.texIndex = 0;// Will be set by DrawGeometry
        bv.entityID = -1;
        batchVertices.push_back(bv);
    }

    renderer->DrawGeometry(batchVertices, meshIndices, _textureID, glm::mat4(1.0f));
}

void Sprite3DComponent::Draw(BatchRenderer2D* renderer) {
    glm::vec3 pos = gameObject->GetPosition();
    glm::vec3 scale = gameObject->GetScale();

    auto* graphics = gameObject->GetApp()->GetGraphicsServer();
    auto* camera = graphics->GetMainCamera();

    glm::mat4 transform;

    if (_billboardMode != BillboardMode::None && camera) {
        glm::vec3 cameraPos = camera->GetEyePosition();
        transform = CalculateBillboardMatrix(pos, cameraPos);
    } else {
        glm::vec3 rot = gameObject->GetRotation();
        transform = glm::translate(glm::mat4(1.0f), pos);
        transform = glm::rotate(transform, rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    glm::vec2 finalSize = _size * glm::vec2(scale.x, scale.y);
    glm::vec2 pivotOffset = (glm::vec2(0.5f, 0.5f) - _pivot) * finalSize;
    transform = glm::translate(transform, glm::vec3(pivotOffset, 0.0f));
    transform = glm::scale(transform, glm::vec3(finalSize.x, finalSize.y, 1.0f));

    if (_mesh) {
        DrawMesh(renderer, transform);
    } else {
        DrawSimpleQuad(renderer, transform);
    }
}
