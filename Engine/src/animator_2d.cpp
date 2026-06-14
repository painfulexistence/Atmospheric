#include "animator_2d.hpp"
#include "console.hpp"
#include "game_object.hpp"
#include "sprite_component.hpp"

Animator2D::Animator2D(GameObject* gameObject) {
    this->gameObject = gameObject;
}

void Animator2D::OnTick(float dt) {
    if (!_isPlaying || !_currentAnimation || _currentAnimation->frames.empty()) return;

    if (!_sprite) {
        _sprite = gameObject->GetComponent<SpriteComponent>();
        if (!_sprite) return;
    }

    _timer += dt;
    const auto& currentFrame = _currentAnimation->frames[_currentFrameIndex];

    if (_timer >= currentFrame.duration) {
        _timer -= currentFrame.duration;
        _currentFrameIndex++;

        if (_currentFrameIndex >= _currentAnimation->frames.size()) {
            if (_currentAnimation->loop) {
                _currentFrameIndex = 0;
            } else {
                _currentFrameIndex = (int)_currentAnimation->frames.size() - 1;
                _isPlaying = false;
            }
        }

        // Update sprite UVs
        const auto& newFrame = _currentAnimation->frames[_currentFrameIndex];
        _sprite->SetUVs(newFrame.uvMin, newFrame.uvMax);
    }
}

void Animator2D::AddAnimation(const std::string& name, const AnimationClip& clip) {
    _animations[name] = clip;
}

void Animator2D::Play(const std::string& name) {
    if (_animations.find(name) == _animations.end()) {
        Console::Get()->Error("Animation not found: " + name);
        return;
    }

    if (_currentAnimation && _currentAnimation->name == name && _isPlaying) return;

    _currentAnimation = &_animations[name];
    _currentFrameIndex = 0;
    _timer = 0.0f;
    _isPlaying = true;

    // Apply first frame immediately
    if (!_sprite) {
        _sprite = gameObject->GetComponent<SpriteComponent>();
    }
    if (_sprite && !_currentAnimation->frames.empty()) {
        const auto& frame = _currentAnimation->frames[0];
        _sprite->SetUVs(frame.uvMin, frame.uvMax);
    }
}

void Animator2D::Stop() {
    _isPlaying = false;
}

void Animator2D::CreateAnimationFromTileset(
  const std::string& name,
  const glm::vec2& tilesetSize,
  const std::vector<int>& tileIndices,
  float frameDuration,
  bool loop
) {
    AnimationClip clip;
    clip.name = name;
    clip.loop = loop;

    for (int index : tileIndices) {
        // Calculate UVs for tile index
        // Assuming tileset is a grid, index 0 is top-left, increasing right then down
        int cols = (int)tilesetSize.x;
        int rows = (int)tilesetSize.y;

        int col = index % cols;
        int row = index / cols;// Note: UV y usually starts from bottom in OpenGL, but tilesets are usually top-down.
                               // We need to be careful. Standard UV (0,0) is bottom-left.
                               // If tileset is top-down, row 0 is at UV y = 1.0 - tileHeight.

        // Let's assume standard top-left origin for tileset indexing logic, but convert to bottom-left UVs.
        float tileW = 1.0f / cols;
        float tileH = 1.0f / rows;

        float uMin = col * tileW;
        float uMax = (col + 1) * tileW;

        // Top-down row to bottom-up UV
        // Row 0 (top) -> y from (1-h) to 1
        float vMax = 1.0f - (row * tileH);
        float vMin = 1.0f - ((row + 1) * tileH);

        AnimationFrame frame;
        frame.uvMin = glm::vec2(uMin, vMin);
        frame.uvMax = glm::vec2(uMax, vMax);
        frame.duration = frameDuration;

        clip.frames.push_back(frame);
    }

    AddAnimation(name, clip);
}
