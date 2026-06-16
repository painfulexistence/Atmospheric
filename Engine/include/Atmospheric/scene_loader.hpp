#pragma once
#include "globals.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Application;
class GameObject;

struct TextProps;

namespace flatbuffers {
    struct CSParseBinary;
    struct NodeTree;
    struct WidgetOptions;
    struct SpriteOptions;
    struct ImageViewOptions;
    struct SingleNodeOptions;
    struct TextOptions;
    struct NodeAction;
}// namespace flatbuffers

// Result of loading a scene file
struct SceneLoadResult {
    GameObject* root = nullptr;
    std::vector<GameObject*> allNodes;
    std::unordered_map<int, GameObject*> nodesByActionTag;
    std::unordered_map<std::string, GameObject*> nodesByName;
    bool success = false;
    std::string error;
};

// Configuration for scene loading
struct SceneLoadConfig {
    std::string basePath = "";// Base path for resolving texture paths
    bool loadTextures = true;
    bool applyTransforms = true;
    CanvasLayer defaultLayer = CanvasLayer::LAYER_WORLD;

    // Callback for custom node type handling
    // Return true if handled, false to use default handling
    std::function<bool(GameObject*, const std::string& classname, const flatbuffers::NodeTree*)> customNodeHandler =
      nullptr;

    // Optional: Override the root node's position
    bool overrideRootPosition = false;
    glm::vec3 rootPosition = glm::vec3(0.0f);

    // Animation settings
    float animationFrameRate = 30.0f;// Frame rate for CSB animations (30fps is standard for 2D games and Unity)
    bool loopAnimations = true;// Whether to loop animations (default: true, like most game engines)
};

class SceneLoader {
public:
    explicit SceneLoader(Application* app);
    ~SceneLoader() = default;

    // Load a scene file (.csb format) and create GameObjects
    // Advanced version using config struct
    SceneLoadResult Load(const std::string& path, const SceneLoadConfig& config);

    // Convenience version: Load with specific position and layer (automatically infers base path)
    SceneLoadResult
      Load(const std::string& path, const glm::vec3& rootPosition, CanvasLayer layer = CanvasLayer::LAYER_WORLD);

    // Load from memory buffer
    SceneLoadResult LoadFromBuffer(const uint8_t* buffer, size_t size, const SceneLoadConfig& config = {});

    // Get supported node types
    static std::vector<std::string> GetSupportedNodeTypes();

private:
    Application* _app;

    // Parse the binary and create node hierarchy
    GameObject* ParseNodeTree(
      const flatbuffers::NodeTree* nodeTree, const SceneLoadConfig& config, SceneLoadResult& result, GameObject* parent
    );

    // Node type handlers
    GameObject* CreateNode(const flatbuffers::WidgetOptions* options, const SceneLoadConfig& config);
    GameObject* CreateSprite(const flatbuffers::SpriteOptions* options, const SceneLoadConfig& config);
    GameObject* CreateImageView(const flatbuffers::ImageViewOptions* options, const SceneLoadConfig& config);
    GameObject* CreateSingleNode(const flatbuffers::SingleNodeOptions* options, const SceneLoadConfig& config);

    // Apply common widget options to a GameObject
    void ApplyWidgetOptions(GameObject* go, const flatbuffers::WidgetOptions* options, const SceneLoadConfig& config);

    // Parse animations from CSB
    void
      ParseAnimations(const flatbuffers::NodeAction* actions, SceneLoadResult& result, const SceneLoadConfig& config);

    // Resolve texture path
    int ResolveTexture(
      const std::string& path, const std::string& plistFile, int resourceType, const SceneLoadConfig& config
    );

    // Create TextProps from CSB TextOptions
    TextProps CreateTextProps(
      const flatbuffers::NodeTree* nodeTree,
      const flatbuffers::WidgetOptions* widgetOptions,
      const SceneLoadConfig& config
    );
};
