#pragma once
#include "component.hpp"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

struct AnimationFrame {
    glm::vec2 uvMin;
    glm::vec2 uvMax;
    float duration;// Duration in seconds
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationFrame> frames;
    bool loop = true;
};

class SpriteComponent;

class Animator2D : public Component {
public:
    Animator2D(GameObject* gameObject);

    std::string GetName() const override {
        return "Animator2D";
    }

    void OnTick(float dt) override;

    void AddAnimation(const std::string& name, const AnimationClip& clip);
    void Play(const std::string& name);
    void Stop();

    // Helper to create animation from tileset
    void CreateAnimationFromTileset(
      const std::string& name,
      const glm::vec2& tilesetSize,
      const std::vector<int>& tileIndices,
      float frameDuration,
      bool loop = true
    );

private:
    SpriteComponent* _sprite = nullptr;
    std::map<std::string, AnimationClip> _animations;
    AnimationClip* _currentAnimation = nullptr;
    int _currentFrameIndex = 0;
    float _timer = 0.0f;
    bool _isPlaying = false;
};
