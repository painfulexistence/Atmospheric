#include "scene_loader.hpp"
#include "action_timeline_component.hpp"
#include "animation_subsystem.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "easing.hpp"
#include "game_object.hpp"
#include "logging.hpp"
#include "sprite_component.hpp"
#include "text_2d_component.hpp"
#include "text_3d_component.hpp"
#include <utility>
#include <vector>

#include "Scene_generated.h"

#include "file_system.hpp"
#include <fstream>
#include <spdlog/spdlog.h>

// Convert CSB easing type to our EasingType enum
static EasingType GetEasingType(int csbType) {
    // CSB easing types (from Cocos Studio):
    // -1 = None/Linear, 0 = Linear
    // 1-3 = Sine (In, Out, InOut)
    // 4-6 = Quad, 7-9 = Cubic, 10-12 = Quart, 13-15 = Quint
    // 16-18 = Expo, 19-21 = Circ, 22-24 = Elastic, 25-27 = Back, 28-30 = Bounce
    if (csbType <= 0) return EasingType::Linear;
    if (csbType <= 30) return static_cast<EasingType>(csbType);
    return EasingType::Linear;
}

SceneLoader::SceneLoader(Application* app) : _app(app) {
}

SceneLoadResult SceneLoader::Load(const std::string& filename, const glm::vec3& rootPosition, CanvasLayer layer) {
    SceneLoadConfig config;
    config.overrideRootPosition = true;
    config.rootPosition = rootPosition;
    config.defaultLayer = layer;
    // basePath will be inferred in the main Load function

    return Load(filename, config);
}

SceneLoadResult SceneLoader::Load(const std::string& filename, const SceneLoadConfig& config) {
    ENGINE_INFO("SceneLoader: Loading CSB file '{}'...", filename);

    SceneLoadResult result;
    auto resolvedOpt = FileSystem::Get().ResolvePath(filename);
    if (!resolvedOpt) {
        result.error = "File not found: " + filename;
        ENGINE_ERROR("SceneLoader: {}", result.error);
        return result;
    }
    const std::string& path = *resolvedOpt;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error = "Failed to open file: " + path;
        ENGINE_ERROR("SceneLoader: {}", result.error);
        return result;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        result.error = "Failed to read file: " + path;
        ENGINE_ERROR("SceneLoader: {}", result.error);
        return result;
    }

    // Derive base path from file path if not specified
    SceneLoadConfig actualConfig = config;
    if (actualConfig.basePath.empty()) {
        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            actualConfig.basePath = path.substr(0, lastSlash + 1);
        }
    }

    SceneLoadResult res = LoadFromBuffer(buffer.data(), buffer.size(), actualConfig);
    if (res.success) {
        ENGINE_INFO("SceneLoader: CSB file '{}' loaded successfully ({} nodes created)", filename, res.allNodes.size());
    }
    return res;
}

SceneLoadResult SceneLoader::LoadFromBuffer(const uint8_t* buffer, size_t size, const SceneLoadConfig& config) {
    SceneLoadResult result;

    // Verify the buffer
    flatbuffers::Verifier verifier(buffer, size);
    if (!flatbuffers::VerifyCSParseBinaryBuffer(verifier)) {
        result.error = "Invalid CSB buffer - verification failed";
        ENGINE_ERROR("SceneLoader: {}", result.error);
        return result;
    }

    // Get the root
    auto csb = flatbuffers::GetCSParseBinary(buffer);
    if (!csb) {
        result.error = "Failed to parse CSB buffer";
        ENGINE_ERROR("SceneLoader: {}", result.error);
        return result;
    }

    // Log version
    if (csb->version()) {
        ENGINE_INFO("SceneLoader: Loading CSB version {}", csb->version()->c_str());
    }

    // Load textures if requested
    if (config.loadTextures && csb->textures()) {
        for (auto tex : *csb->textures()) {
            if (tex) {
                std::string texPath = config.basePath + tex->c_str();
                try {
                    AssetManager::Get().CreateTexture(texPath);
                    ENGINE_DEBUG("SceneLoader: Loaded texture {}", texPath);
                } catch (const std::exception& e) {
                    ENGINE_WARN("SceneLoader: Failed to preload texture '{}': {}", texPath, e.what());
                }
            }
        }
    }

    // Also load PNG textures
    if (config.loadTextures && csb->texturePngs()) {
        for (auto tex : *csb->texturePngs()) {
            if (tex) {
                std::string texPath = config.basePath + tex->c_str();
                try {
                    AssetManager::Get().CreateTexture(texPath);
                    ENGINE_DEBUG("SceneLoader: Loaded PNG texture {}", texPath);
                } catch (const std::exception& e) {
                    ENGINE_WARN("SceneLoader: Failed to preload PNG texture '{}': {}", texPath, e.what());
                }
            }
        }
    }

    // Parse node tree
    if (csb->nodeTree()) {
        result.root = ParseNodeTree(csb->nodeTree(), config, result, nullptr);
        result.success = (result.root != nullptr);

        // Apply root position override if requested
        if (result.success && config.overrideRootPosition) {
            result.root->SetPosition(config.rootPosition);
        }
    } else {
        result.error = "CSB has no node tree";
        ENGINE_WARN("SceneLoader: {}", result.error);
    }

    // Parse animations (csb->action())
    if (csb->action()) {
        ParseAnimations(csb->action(), result, config);
    }

    return result;
}

// ... existing ParseNodeTree ...

void SceneLoader::ParseAnimations(
    const flatbuffers::NodeAction* actions, SceneLoadResult& result, const SceneLoadConfig& config
) {
    if (!actions) {
        ENGINE_DEBUG("SceneLoader: No Action data in CSB.");
        return;
    }
    if (!actions->timeLines()) {
        ENGINE_DEBUG("SceneLoader: Action data present but no TimeLines.");
        return;
    }

    // Use configured frame rate (Unity typically uses 30fps, Cocos uses 60fps)
    float frameRate = config.animationFrameRate;

    // Read speed from CSB (1.0 = normal speed, 2.0 = double speed)
    float speed = actions->speed();
    if (speed <= 0.0f) speed = 1.0f;// Fallback to normal speed if invalid

    ENGINE_INFO(
        "SceneLoader: Parsing {} timelines at {}fps, speed={}, loop={}",
        actions->timeLines()->size(),
        frameRate,
        speed,
        config.loopAnimations
    );

    // CSB frames are already keyframes, so each (node, property) timeline maps
    // directly onto an ActionTrack: frameIndex/frameRate/speed → absolute key
    // time, easingData → per-key easing. Tracks are grouped into one
    // ActionTimeline per node (concurrent properties become concurrent tracks),
    // registered in the AnimationLibrary and auto-played on an
    // ActionTimelineComponent — replacing the former keyframe→Action→RunAction
    // double translation.
    auto* animSub = AnimationSubsystem::Get();
    if (!animSub) {
        spdlog::warn("SceneLoader: AnimationSubsystem unavailable; skipping CSB animations.");
        return;
    }

    // Per-node accumulator, insertion-ordered for deterministic instantiation.
    std::vector<std::pair<GameObject*, ActionTimeline>> perNode;
    auto timelineFor = [&](GameObject* node) -> ActionTimeline& {
        for (auto& [go, tl] : perNode)
            if (go == node) return tl;
        perNode.push_back({ node, ActionTimeline{} });
        return perNode.back().second;
    };
    const auto keyTime = [&](int frameIndex) {
        float t = frameIndex / frameRate / speed;
        return t < 0.0f ? 0.0f : t;
    };

    for (auto timeline : *actions->timeLines()) {
        if (!timeline || !timeline->frames() || timeline->frames()->size() == 0) {
            ENGINE_WARN("SceneLoader: skipping empty timeline.");
            continue;
        }

        int actionTag = timeline->actionTag();
        ENGINE_INFO(
            "SceneLoader: Processing timeline for ActionTag {} Property {}", actionTag, timeline->property()->c_str()
        );

        auto it = result.nodesByActionTag.find(actionTag);
        if (it == result.nodesByActionTag.end()) {
            ENGINE_WARN(
                "SceneLoader: ActionTag {} not found in scene nodes (Map size: {}).",
                actionTag,
                result.nodesByActionTag.size()
            );
            continue;
        }

        GameObject* target = it->second;
        if (!target) continue;

        std::string property = timeline->property()->c_str();
        ActionTrack track;

        if (property == "Position") {
            track.property = ActionProperty::Position;
            for (auto frame : *timeline->frames()) {
                if (frame->pointFrame() && frame->pointFrame()->position()) {
                    EasingType easing = EasingType::Linear;
                    if (frame->pointFrame()->easingData())
                        easing = GetEasingType(frame->pointFrame()->easingData()->type());
                    auto pos = frame->pointFrame()->position();
                    track.keys.push_back(
                        { keyTime(frame->pointFrame()->frameIndex()),
                          glm::vec4(pos->x(), pos->y(), 0.0f, 0.0f),
                          easing }
                    );
                }
            }
        } else if (property == "Scale") {
            track.property = ActionProperty::Scale;
            for (auto frame : *timeline->frames()) {
                if (frame->scaleFrame() && frame->scaleFrame()->scale()) {
                    EasingType easing = EasingType::Linear;
                    if (frame->scaleFrame()->easingData())
                        easing = GetEasingType(frame->scaleFrame()->easingData()->type());
                    auto scale = frame->scaleFrame()->scale();
                    track.keys.push_back(
                        { keyTime(frame->scaleFrame()->frameIndex()),
                          glm::vec4(scale->scaleX(), scale->scaleY(), 1.0f, 0.0f),
                          easing }
                    );
                }
            }
        } else if (property == "RotationSkew") {
            track.property = ActionProperty::Rotation;
            for (auto frame : *timeline->frames()) {
                if (frame->intFrame()) {
                    EasingType easing = EasingType::Linear;
                    if (frame->intFrame()->easingData())
                        easing = GetEasingType(frame->intFrame()->easingData()->type());
                    // CSB stores rotation in degrees; convert to radians (Z axis).
                    float rotRad = glm::radians(static_cast<float>(frame->intFrame()->value()));
                    track.keys.push_back(
                        { keyTime(frame->intFrame()->frameIndex()), glm::vec4(0.0f, 0.0f, rotRad, 0.0f), easing }
                    );
                }
            }
            if (track.keys.empty()) {
                ENGINE_WARN(
                    "SceneLoader: RotationSkew timeline found on '{}' but frames contain no IntFrame data.",
                    target->GetName()
                );
            }
        } else if (property == "CColor") {
            track.property = ActionProperty::Color;
            for (auto frame : *timeline->frames()) {
                if (frame->colorFrame() && frame->colorFrame()->color()) {
                    auto* colorData = frame->colorFrame()->color();
                    EasingType easing = EasingType::Linear;
                    if (frame->colorFrame()->easingData())
                        easing = GetEasingType(frame->colorFrame()->easingData()->type());
                    track.keys.push_back(
                        { keyTime(frame->colorFrame()->frameIndex()),
                          glm::vec4(
                              colorData->r() / 255.0f,
                              colorData->g() / 255.0f,
                              colorData->b() / 255.0f,
                              colorData->a() / 255.0f
                          ),
                          easing }
                    );
                }
            }
        }

        if (!track.keys.empty()) timelineFor(target).tracks.push_back(std::move(track));
    }

    // Instantiate one auto-playing timeline component per animated node.
    for (auto& [node, tl] : perNode) {
        if (tl.tracks.empty()) continue;
        tl.name = node->GetName() + "_csb";
        TimelineHandle handle = animSub->Library().AddTimeline(std::move(tl));

        auto* atc = node->GetComponent<ActionTimelineComponent>();
        if (!atc) atc = static_cast<ActionTimelineComponent*>(node->AddComponent<ActionTimelineComponent>());
        atc->AddTimeline(handle);
        atc->SetWrapMode(config.loopAnimations ? WrapMode::Loop : WrapMode::Once);
        atc->Play(node->GetName() + "_csb");
    }
}

GameObject* SceneLoader::ParseNodeTree(
    const flatbuffers::NodeTree* nodeTree, const SceneLoadConfig& config, SceneLoadResult& result, GameObject* parent
) {
    if (!nodeTree) return nullptr;

    std::string classname = nodeTree->classname() ? nodeTree->classname()->c_str() : "Node";
    GameObject* go = nullptr;

    // Check for custom handler first
    if (config.customNodeHandler && config.customNodeHandler(nullptr, classname, nodeTree)) {
        // Custom handler will create and return the GameObject
        // For now, we don't support this pattern - custom handler should return via result
        ENGINE_DEBUG("SceneLoader: Custom handler used for {}", classname);
    }

    // Get the options
    const flatbuffers::WidgetOptions* widgetOptions = nullptr;
    if (nodeTree->options() && nodeTree->options()->data()) {
        widgetOptions = nodeTree->options()->data();
    }

    // Create node based on classname
    if (classname == "Sprite") {
        // For Sprite, we need to get SpriteOptions from the options
        // CSB stores type-specific options in the Options table
        // The actual SpriteOptions is accessed via the data field with correct type
        go = CreateNode(widgetOptions, config);

        // Try to get sprite-specific data
        // In CSB format, sprite options include fileNameData
        if (widgetOptions) {
            // For POC: Create a sprite with the widget options
            // Full implementation would need to access SpriteOptions
            SpriteProps props;
            if (widgetOptions->size()) {
                props.size = glm::vec2(widgetOptions->size()->width(), widgetOptions->size()->height());
            }
            if (widgetOptions->anchorPoint()) {
                props.pivot = glm::vec2(widgetOptions->anchorPoint()->scaleX(), widgetOptions->anchorPoint()->scaleY());
            }
            if (widgetOptions->color()) {
                props.color = glm::vec4(
                    widgetOptions->color()->r() / 255.0f,
                    widgetOptions->color()->g() / 255.0f,
                    widgetOptions->color()->b() / 255.0f,
                    widgetOptions->alpha() / 255.0f
                );
            }
            props.layer = config.defaultLayer;
            props.flipX = widgetOptions->flipX();
            props.flipY = widgetOptions->flipY();
            props.zOrder = widgetOptions->zOrder();

            // Apply sprite component
            if (go) {
                go->AddComponent<SpriteComponent>(props);
            }
        }
    } else if (classname == "ImageView") {
        go = CreateNode(widgetOptions, config);
        if (widgetOptions && go) {
            SpriteProps props;
            if (widgetOptions->size()) {
                props.size = glm::vec2(widgetOptions->size()->width(), widgetOptions->size()->height());
            }
            if (widgetOptions->anchorPoint()) {
                props.pivot = glm::vec2(widgetOptions->anchorPoint()->scaleX(), widgetOptions->anchorPoint()->scaleY());
            }
            if (widgetOptions->color()) {
                props.color = glm::vec4(
                    widgetOptions->color()->r() / 255.0f,
                    widgetOptions->color()->g() / 255.0f,
                    widgetOptions->color()->b() / 255.0f,
                    widgetOptions->alpha() / 255.0f
                );
            }
            props.layer = config.defaultLayer;
            props.flipX = widgetOptions->flipX();
            props.flipY = widgetOptions->flipY();
            props.zOrder = widgetOptions->zOrder();
            go->AddComponent<SpriteComponent>(props);
        }
    } else if (classname == "Text") {
        go = CreateNode(widgetOptions, config);
        if (go) {
            go->AddComponent<Text2DComponent>(CreateTextProps(nodeTree, widgetOptions, config));
            ENGINE_DEBUG(
                "SceneLoader: Created Text node '{}'",
                widgetOptions && widgetOptions->name() ? widgetOptions->name()->c_str() : "Text"
            );
        }
    } else if (classname == "Node" || classname == "SingleNode") {
        go = CreateNode(widgetOptions, config);
    } else {
        // Unknown type - create as basic node and log warning
        ENGINE_WARN("SceneLoader: Unsupported node type '{}', creating as basic node", classname);
        go = CreateNode(widgetOptions, config);
    }

    if (!go) {
        ENGINE_ERROR("SceneLoader: Failed to create GameObject for {}", classname);
        return nullptr;
    }

    // Apply widget options
    if (widgetOptions) {
        ApplyWidgetOptions(go, widgetOptions, config);
    }

    // Set parent relationship
    if (parent) {
        go->parent = parent;
    }

    // Track in result
    result.allNodes.push_back(go);
    if (widgetOptions) {
        if (widgetOptions->actionTag() != 0) {
            result.nodesByActionTag[widgetOptions->actionTag()] = go;
        }
        if (widgetOptions->name()) {
            result.nodesByName[widgetOptions->name()->c_str()] = go;
        }
    }

    // Process children
    if (nodeTree->children()) {
        for (auto child : *nodeTree->children()) {
            ParseNodeTree(child, config, result, go);
        }
    }

    return go;
}

GameObject* SceneLoader::CreateNode(const flatbuffers::WidgetOptions* options, const SceneLoadConfig& config) {
    glm::vec3 position(0.0f);
    glm::vec3 rotation(0.0f);
    glm::vec3 scale(1.0f);

    if (options && config.applyTransforms) {
        if (options->position()) {
            position = glm::vec3(options->position()->x(), options->position()->y(), 0.0f);
        }
        if (options->rotationSkew()) {
            // CSB uses rotationSkewX for Z rotation in 2D (degrees -> radians for CreateGameObject)
            float rotDeg = options->rotationSkew()->rotationSkewX();
            rotation = glm::vec3(0.0f, 0.0f, glm::radians(rotDeg));
        }
        if (options->scale()) {
            scale = glm::vec3(options->scale()->scaleX(), options->scale()->scaleY(), 1.0f);
        }
    }

    return _app->CreateGameObject(position, rotation, scale);
}

GameObject* SceneLoader::CreateSprite(const flatbuffers::SpriteOptions* options, const SceneLoadConfig& config) {
    auto go = _app->CreateGameObject();
    if (!go || !options) return go;

    SpriteProps props;

    // Get base widget options
    auto* widgetOpts = options->nodeOptions();
    if (widgetOpts) {
        if (widgetOpts->size()) {
            props.size = glm::vec2(widgetOpts->size()->width(), widgetOpts->size()->height());
        }
        if (widgetOpts->anchorPoint()) {
            props.pivot = glm::vec2(widgetOpts->anchorPoint()->scaleX(), widgetOpts->anchorPoint()->scaleY());
        }
        if (widgetOpts->color()) {
            props.color = glm::vec4(
                widgetOpts->color()->r() / 255.0f,
                widgetOpts->color()->g() / 255.0f,
                widgetOpts->color()->b() / 255.0f,
                widgetOpts->alpha() / 255.0f
            );
        }
    }

    // Load texture from fileNameData
    if (options->fileNameData() && options->fileNameData()->path()) {
        props.texture = ResolveTexture(
            options->fileNameData()->path()->c_str(),
            options->fileNameData()->plistFile() ? options->fileNameData()->plistFile()->c_str() : "",
            options->fileNameData()->resourceType(),
            config
        );
    }

    props.layer = config.defaultLayer;
    go->AddComponent<SpriteComponent>(props);

    return go;
}

GameObject* SceneLoader::CreateImageView(const flatbuffers::ImageViewOptions* options, const SceneLoadConfig& config) {
    auto go = _app->CreateGameObject();
    if (!go || !options) return go;

    SpriteProps props;

    auto* widgetOpts = options->widgetOptions();
    if (widgetOpts) {
        if (widgetOpts->size()) {
            props.size = glm::vec2(widgetOpts->size()->width(), widgetOpts->size()->height());
        }
        if (widgetOpts->anchorPoint()) {
            props.pivot = glm::vec2(widgetOpts->anchorPoint()->scaleX(), widgetOpts->anchorPoint()->scaleY());
        }
        if (widgetOpts->color()) {
            props.color = glm::vec4(
                widgetOpts->color()->r() / 255.0f,
                widgetOpts->color()->g() / 255.0f,
                widgetOpts->color()->b() / 255.0f,
                widgetOpts->alpha() / 255.0f
            );
        }
    }

    // Load texture
    if (options->fileNameData() && options->fileNameData()->path()) {
        props.texture = ResolveTexture(
            options->fileNameData()->path()->c_str(),
            options->fileNameData()->plistFile() ? options->fileNameData()->plistFile()->c_str() : "",
            options->fileNameData()->resourceType(),
            config
        );
    }

    props.layer = config.defaultLayer;
    go->AddComponent<SpriteComponent>(props);

    return go;
}

GameObject*
    SceneLoader::CreateSingleNode(const flatbuffers::SingleNodeOptions* options, const SceneLoadConfig& config) {
    return CreateNode(options ? options->nodeOptions() : nullptr, config);
}

void SceneLoader::ApplyWidgetOptions(
    GameObject* go, const flatbuffers::WidgetOptions* options, const SceneLoadConfig& config
) {
    if (!go || !options) return;

    // Set name
    if (options->name()) {
        go->SetName(options->name()->c_str());
    }

    // Set visibility
    go->SetActive(options->visible());

    // Transform is already applied in CreateNode, but we can update here if needed
    if (config.applyTransforms) {
        if (options->position()) {
            go->SetPosition(glm::vec3(options->position()->x(), options->position()->y(), 0.0f));
        }
        if (options->rotationSkew()) {
            go->SetEulerAngles(glm::vec3(0.0f, 0.0f, options->rotationSkew()->rotationSkewX()));
        }
        if (options->scale()) {
            go->SetScale(glm::vec3(options->scale()->scaleX(), options->scale()->scaleY(), 1.0f));
        }
    }
}

int SceneLoader::ResolveTexture(
    const std::string& path, const std::string& plistFile, int resourceType, const SceneLoadConfig& config
) {
    // Resource types in CSB:
    // 0 = Normal file
    // 1 = Plist (sprite sheet)
    // 2 = Default (built-in)

    if (path.empty()) {
        return -1;
    }

    std::string fullPath = config.basePath + path;

    // For now, only handle normal files (type 0)
    // TODO: Add plist/sprite sheet support
    if (resourceType == 1 && !plistFile.empty()) {
        ENGINE_WARN("SceneLoader: Plist sprite sheets not yet supported, loading as regular texture: {}", path);
    }

    try {
        // Try to get existing texture or create new one
        GLuint texID = AssetManager::Get().GetTexture(fullPath);
        if (texID == 0) {
            texID = AssetManager::Get().CreateTexture(fullPath);
        }
        return static_cast<int>(texID);
    } catch (const std::exception& e) {
        ENGINE_WARN("SceneLoader: Failed to load texture '{}': {}", fullPath, e.what());
        return 0;// Return invalid texture ID
    }
}

std::vector<std::string> SceneLoader::GetSupportedNodeTypes() {
    return { "Node", "SingleNode", "Sprite", "ImageView", "Text" };
}

Text2DProps SceneLoader::CreateTextProps(
    const flatbuffers::NodeTree* nodeTree,
    const flatbuffers::WidgetOptions* widgetOptions,
    const SceneLoadConfig& config
) {
    Text2DProps props;

    // CSB stores TextOptions in the Options.data field for Text nodes
    // We can reinterpret the WidgetOptions pointer as TextOptions
    const flatbuffers::TextOptions* textOptions = nullptr;
    if (nodeTree && nodeTree->options()) {
        // The data field is stored as WidgetOptions but actually contains TextOptions for Text nodes
        textOptions = reinterpret_cast<const flatbuffers::TextOptions*>(nodeTree->options()->data());
    }

    // Read from TextOptions if available
    if (textOptions) {
        // Get text content
        if (textOptions->text()) {
            props.text = textOptions->text()->c_str();
        }

        // Get font settings
        props.fontSize = static_cast<float>(textOptions->fontSize());
        if (props.fontSize <= 0) props.fontSize = 24.0f;

        if (textOptions->fontName()) {
            std::string fontPath = textOptions->fontName()->c_str();
            if (!fontPath.empty()) {
                props.font = GraphicsSubsystem::Get()->LoadFont(fontPath, props.fontSize);
            }
        }

        // Get alignment
        props.hAlign = static_cast<TextHAlignment>(textOptions->hAlignment());
        props.vAlign = static_cast<TextVAlignment>(textOptions->vAlignment());

        // Get area size if custom size is enabled
        if (textOptions->isCustomSize()) {
            props.size =
                glm::vec2(static_cast<float>(textOptions->areaWidth()), static_cast<float>(textOptions->areaHeight()));
        }

        // Get widget options from TextOptions
        auto* textWidgetOpts = textOptions->widgetOptions();
        if (textWidgetOpts) {
            if (!textOptions->isCustomSize() && textWidgetOpts->size()) {
                props.size = glm::vec2(textWidgetOpts->size()->width(), textWidgetOpts->size()->height());
            }
            if (textWidgetOpts->anchorPoint()) {
                props.pivot =
                    glm::vec2(textWidgetOpts->anchorPoint()->scaleX(), textWidgetOpts->anchorPoint()->scaleY());
            }
            if (textWidgetOpts->color()) {
                props.color = glm::vec4(
                    textWidgetOpts->color()->r() / 255.0f,
                    textWidgetOpts->color()->g() / 255.0f,
                    textWidgetOpts->color()->b() / 255.0f,
                    textWidgetOpts->alpha() / 255.0f
                );
            }
            props.zOrder = textWidgetOpts->zOrder();
        }
    } else if (widgetOptions) {
        // Fallback to basic widget options
        if (widgetOptions->size()) {
            props.size = glm::vec2(widgetOptions->size()->width(), widgetOptions->size()->height());
        }
        if (widgetOptions->anchorPoint()) {
            props.pivot = glm::vec2(widgetOptions->anchorPoint()->scaleX(), widgetOptions->anchorPoint()->scaleY());
        }
        if (widgetOptions->color()) {
            props.color = glm::vec4(
                widgetOptions->color()->r() / 255.0f,
                widgetOptions->color()->g() / 255.0f,
                widgetOptions->color()->b() / 255.0f,
                widgetOptions->alpha() / 255.0f
            );
        }
        props.zOrder = widgetOptions->zOrder();
    }

    props.layer = config.defaultLayer;

    return props;
}
