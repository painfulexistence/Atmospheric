#include "graphics_subsystem.hpp"
#include "Atmospheric/window.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "config.hpp"
#include "frustum.hpp"
#include "game_object.hpp"
#include "gfx_factory.hpp"
#include "gl_render_target.hpp"
#include "light_component.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"
#include "renderer.hpp"
#include "sprite_component.hpp"
#include "stb_image.h"
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <sstream>
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <cstddef>

GraphicsSubsystem* GraphicsSubsystem::_instance = nullptr;

GraphicsSubsystem::GraphicsSubsystem() {
    if (_instance != nullptr) throw std::runtime_error("GraphicsSubsystem is already initialized!");

    _instance = this;
}

GraphicsSubsystem::~GraphicsSubsystem() {
    if (renderer) {
        renderer->Cleanup();
    }
}

void GraphicsSubsystem::Init(Application* app) {
    Subsystem::Init(app);

    stbi_set_flip_vertically_on_load(true);

#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    if (gladLoadGLLoader((GLADloadproc)Window::GetProcAddress()) <= 0)
        throw std::runtime_error("Failed to initialize OpenGL!");
#endif
    GfxFactory::Init();

    auto window = Window::Get();
    auto [width, height] = window->GetPhysicalSize();
    auto [logicalWidth, logicalHeight] = window->GetLogicalSize();

    // ── Common scene objects (backend-independent) ────────────────────────────
    canvasDrawList.reserve(1 << 16);
    debugLines.reserve(1 << 16);

    CameraProps defaultCameraProps{};
    if (app->GetConfig().preset == "2D") {
        defaultCameraProps.isOrthographic = true;
        defaultCameraProps.orthographic = {
            .width = static_cast<float>(logicalWidth),
            .height = static_cast<float>(logicalHeight),
            .nearClip = -100.0f,
            .farClip = 1000.0f
        };
        // 2D horizontal angle defaults to pointing down -Z/forward (Yaw equivalent to -half_pi)
        defaultCameraProps.horizontalAngle = -glm::half_pi<float>();
    } else {
        defaultCameraProps.isOrthographic = false;
        defaultCameraProps.perspective = {
            .fieldOfView = 45.0f,
            .aspectRatio = static_cast<float>(width) / static_cast<float>(height),
            .nearClip = 0.1f,
            .farClip = 1000.0f
        };
    }
    defaultCamera =
      dynamic_cast<CameraComponent*>(app->GetDefaultGameObject()->AddComponent<CameraComponent>(defaultCameraProps));

    if (app->GetConfig().preset == "2D") {
        defaultCamera->gameObject->SetPosition(glm::vec3(static_cast<float>(logicalWidth) * 0.5f, static_cast<float>(logicalHeight) * 0.5f, 0.0f));
    }

    defaultLight = dynamic_cast<LightComponent*>(app->GetDefaultGameObject()->AddComponent<LightComponent>(LightProps{
      .type = LightType::Directional,
      .ambient = glm::vec3(1.0f, 1.0f, 1.0f),
      .diffuse = glm::vec3(1.0f, 1.0f, 1.0f),
      .specular = glm::vec3(1.0f, 1.0f, 1.0f),
      .direction = glm::vec3(0.0f, -1.0f, 0.0f),
      .intensity = 1.0f,
      .castShadow = false }));

#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        // WebGPU path: GPUCanvasPass handles all rendering; no GL objects needed.
        return;
    }
#endif

    // ── OpenGL / WebGL 2 path ─────────────────────────────────────────────────
    AssetManager::Get().LoadDefaultShaders();

#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPrimitiveRestartIndex(0xFFFF);
    glPatchParameteri(GL_PATCH_VERTICES, 4);
#endif
    glLineWidth(2.0f);
    glCullFace(GL_BACK);
#if MSAA_ON && !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glEnable(GL_MULTISAMPLE);
#endif

    renderer = std::make_unique<Renderer>();
    renderer->Init(width, height);
    window->AddFramebufferResizeCallback([this](int newWidth, int newHeight) {
        renderer->Resize(newWidth, newHeight);
    });

    debugLineMesh = std::make_unique<Mesh>(MeshType::DEBUG);
    debugLineMesh->updateFreq = UpdateFrequency::Dynamic;

    canvasMesh = std::make_unique<Mesh>(MeshType::CANVAS);
    canvasMesh->updateFreq = UpdateFrequency::Dynamic;

    try { debugShader = AssetManager::Get().GetShader("debug_line"); } catch (...) { debugShader = nullptr; }
    try { canvasShader = AssetManager::Get().GetShader("canvas"); }     catch (...) { canvasShader = nullptr; }
}

void GraphicsSubsystem::Process(float dt) {
    // No updates here since no gameplay logic is required
}

// NOTES: this only fills in command buffers, rendering should be done by the renderer
void GraphicsSubsystem::Render(CameraComponent* camera, float dt) {
    ZoneScopedN("GraphicsSubsystem::Render");

#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        // WebGPU: canvas draw list is consumed by GPUCanvasPass in the render loop.
        // No GL renderer to drive here.
        return;
    }
#endif

    if (!camera) {
        camera = defaultCamera;
        if (!camera) return;
    }

    Frustum frustum(camera->GetProjectionMatrix() * camera->GetViewMatrix());

    // Submit render commands
    int totalCount = 0;
    int culledCount = 0;
    for (auto r : renderables) {
        totalCount++;
        if (!r->gameObject->isActive) continue;

        MeshHandle meshHandle = r->GetMesh();
        if (!meshHandle.IsValid()) continue;
        Mesh* mesh = AssetManager::Get().GetMeshPtr(meshHandle);
        if (!mesh) continue;

        // Frustum Culling
        const auto& transform = r->gameObject->GetTransform();

        if (FRUSTUM_CULLING_ON) {
            ZoneScopedN("Frustum Culling");
            const auto& boundingBox = mesh->GetBoundingBox();
            std::array<glm::vec3, 8> worldBounds;
            bool hasValidBounds = false;
            for (int i = 0; i < 8; ++i) {
                if (boundingBox[i] != glm::vec3(0.0f)) {
                    hasValidBounds = true;
                }
                worldBounds[i] = transform * glm::vec4(boundingBox[i], 1.0f);
            }
            if (hasValidBounds && !frustum.Intersects(worldBounds)) {
                culledCount++;
                continue;
            }
        }

        RenderCommand cmd{ .mesh = meshHandle, .transform = transform };
        renderer->SubmitCommand(cmd);
    }

    if (totalCount > 0) {
        static int frameCounter = 0;
        if (frameCounter++ % 60 == 0) {
            ConsoleSubsystem::Get()->Info(fmt::format("Culling: total {} culled {}", totalCount, culledCount));
        }
    }

    // TODO: migrate canvas drawables to use commands
    // We are now using BatchRenderer2D inside CanvasPass::Execute,
    // but the data collection happens here or we pass the list to Renderer.
    // Actually, GraphicsSubsystem::Render calls renderer->RenderFrame(this, dt),
    // and inside RenderFrame, it calls CanvasPass::Execute.
    // So we should keep the list of sprites here, but we don't need to push quads manually anymore.
    // The CanvasPass will iterate over canvasDrawables and call BatchRenderer2D::DrawQuad.

    // However, the original code pushed quads to `canvasDrawList`.
    // We should clear that list or stop using it.
    // Let's stop using `canvasDrawList` and instead let `CanvasPass` access `canvasDrawables`.

    renderer->RenderFrame(this, dt);
}

void GraphicsSubsystem::DrawImGui(float dt) {
    ZoneScopedN("GraphicsSubsystem::DrawImGui");
    if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Frame rate: %.3f ms/frame (%.1f FPS)", 1000.0f * dt, 1.0f / dt);
        ImGui::Text(
          "Average frame rate: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate
        );
        ImGui::ColorEdit3("Clear color", (float*)&renderer->clearColor);
        if (auto* vcp = renderer->GetPass<VoxelChunkPass>()) {
            const char* paletteNames[] = {
                "1 - Warm Pink/Gold", "2 - Cool Blue/Purple", "3 - Earthy Green",
                "4 - Forest",         "5 - Soft Cool (default)", "6 - Vivid Mint/Coral"
            };
            ImGui::Combo("Voxel Palette", &vcp->paletteIndex, paletteNames, 6);
        }
        if (auto* bloom = renderer->GetPass<BloomPass>()) {
            ImGui::Checkbox("Bloom", &bloom->enabled);
        }
        if (auto* pp = renderer->GetPass<PostProcessPass>()) {
            ImGui::Checkbox("Tonemap", &pp->tonemapEnabled);
            ImGui::Checkbox("Chromatic Aberration", &pp->caEnabled);
            const char* effectNames[] = { "None", "CRT", "VHS", "Color Grading", "Posterize", "Sobel", "Edges", "Vignette" };
            int effectIdx = (int)pp->postEffect;
            if (ImGui::Combo("Post Effect", &effectIdx, effectNames, 8))
                pp->postEffect = (PostEffect)effectIdx;
        }
        ImGui::Text("Opaque Queue Size: %d", static_cast<int>(renderer->GetOpaqueQueue().size()));

#ifdef AE_GPU_TIMER_ENABLED
        ImGui::Separator();
        ImGui::Checkbox("Perf GPU", &renderer->GpuProfilingEnabled());
        if (renderer->GpuProfilingEnabled()) {
            if (ImGui::TreeNode("GPU Pass Timings")) {
                auto timings = renderer->GetTimings();
                for (auto& [name, ms] : timings) {
                    bool isTotal = (name == "[Total]");
                    if (isTotal) ImGui::Separator();
                    ImGui::Text("%-26s %.3f ms", name.c_str(), ms);
                }
                ImGui::TreePop();
            }
        }
#endif
        ImGui::Separator();

        if (ImGui::TreeNode("Cameras")) {
            for (auto c : cameras) {
                ImGui::Text("%s (camera)", c->gameObject->GetName().c_str());
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Lights")) {
            for (auto l : directionalLights) {
                const std::string& name = l->gameObject->GetName();
                if (ImGui::TreeNode(name.c_str(), "%s (light)", name.c_str())) {
                    ImGui::Text("Direction: %.3f, %.3f, %.3f", l->direction.x, l->direction.y, l->direction.z);
                    ImGui::Text("Ambient: %.3f, %.3f, %.3f", l->ambient.x, l->ambient.y, l->ambient.z);
                    ImGui::Text("Diffuse: %.3f, %.3f, %.3f", l->diffuse.x, l->diffuse.y, l->diffuse.z);
                    ImGui::Text("Specular: %.3f, %.3f, %.3f", l->specular.x, l->specular.y, l->specular.z);
                    ImGui::Text("Intensity: %.3f", l->intensity);
                    ImGui::Text("Cast shadow: %s", l->castShadow ? "true" : "false");
                    ImGui::TreePop();
                }
            }
            for (auto l : pointLights) {
                const std::string& name = l->gameObject->GetName();
                if (ImGui::TreeNode(name.c_str(), "%s (light)", name.c_str())) {
                    ImGui::Text("Attenuation: %.3f, %.3f, %.3f", l->attenuation.x, l->attenuation.y, l->attenuation.z);
                    ImGui::Text("Ambient: %.3f, %.3f, %.3f", l->ambient.x, l->ambient.y, l->ambient.z);
                    ImGui::Text("Diffuse: %.3f, %.3f, %.3f", l->diffuse.x, l->diffuse.y, l->diffuse.z);
                    ImGui::Text("Specular: %.3f, %.3f, %.3f", l->specular.x, l->specular.y, l->specular.z);
                    ImGui::Text("Intensity: %.3f", l->intensity);
                    ImGui::Text("Cast shadow: %s", l->castShadow ? "true" : "false");
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Canvas")) {
            ImGui::Text("Canvas quads: %d", _canvasQuadCount);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Physics Debug")) {
            ImGui::Text("Debug line count: %d", _debugLineCount);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Textures")) {
            for (auto t : renderer->uniShadowMaps) {
                if (ImGui::TreeNode(fmt::format("Directional shadow map #{}", t).c_str())) {
                    ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(64, 64));
                    ImGui::TreePop();
                }
            }
            for (auto t : renderer->omniShadowMaps) {
                // FIXME: cubemap textures are not supported yet
                // if (ImGui::TreeNode(fmt:: format("Point shadow map #{}", t).c_str())) {
                //     ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(64, 64));
                //     ImGui::TreePop();
                // }
            }
            ImGui::Separator();
            if (ImGui::TreeNode(fmt::format("Scene Color RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)static_cast<uint32_t>(renderer->sceneRT ? renderer->sceneRT->GetTextureID() : 0), ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("Scene Depth RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)static_cast<uint32_t>(renderer->sceneRT ? renderer->sceneRT->GetDepthTextureID() : 0), ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("MSAA Resolve RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)static_cast<uint32_t>(renderer->msaaResolveRT ? renderer->msaaResolveRT->GetTextureID() : 0), ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("GBuffer Position RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)renderer->gBuffer.positionRT, ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("GBuffer Normal RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)renderer->gBuffer.normalRT, ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("GBuffer Albedo RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)renderer->gBuffer.albedoRT, ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("GBuffer Material RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)renderer->gBuffer.materialRT, ImVec2(64, 64));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(fmt::format("GBuffer Depth RT").c_str())) {
                ImGui::Image((ImTextureID)(intptr_t)renderer->gBuffer.depthRT, ImVec2(64, 64));
                ImGui::TreePop();
            }
            ImGui::Separator();
            auto& assetManager = AssetManager::Get();
            for (auto t : assetManager.GetDefaultTextures()) {
                if (ImGui::TreeNode(fmt::format("Default Tex #{}", t).c_str())) {
                    ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(64, 64));
                    ImGui::TreePop();
                }
            }
            ImGui::Separator();
            for (auto t : assetManager.GetTextures()) {
                if (ImGui::TreeNode(fmt::format("Tex #{}", t).c_str())) {
                    ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(64, 64));
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Materials")) {
            auto& assetManager = AssetManager::Get();
            for (const auto& m : assetManager.GetMaterials()) {
                if (!m) continue;// slot emptied by RemoveMaterial/UnloadSceneAssets
                if (ImGui::TreeNode("Mat")) {
                    ImGui::Text("Base Map ID: %d", static_cast<int>(m->baseMap));
                    ImGui::Text("Normal Map ID: %d", static_cast<int>(m->normalMap));
                    ImGui::Text("AO Map ID: %d", static_cast<int>(m->aoMap));
                    ImGui::Text("Roughness Map ID: %d", static_cast<int>(m->roughnessMap));
                    ImGui::Text("Metallic Map ID: %d", static_cast<int>(m->metallicMap));
                    ImGui::Text("Height Map ID: %d", static_cast<int>(m->heightMap));
                    ImGui::Text("Ambient: %.3f, %.3f, %.3f", m->ambient.x, m->ambient.y, m->ambient.z);
                    ImGui::Text("Diffuse: %.3f, %.3f, %.3f", m->diffuse.x, m->diffuse.y, m->diffuse.z);
                    ImGui::Text("Specular: %.3f, %.3f, %.3f", m->specular.x, m->specular.y, m->specular.z);
                    ImGui::Text("Shininess: %.3f", m->shininess);
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
}

void GraphicsSubsystem::Reset() {
    defaultCamera = nullptr;
    defaultLight = nullptr;
    cameras.clear();
    directionalLights.clear();
    pointLights.clear();
    sunComponents.clear();
    renderables.clear();
}

ShaderProgram* GraphicsSubsystem::GetShader(const std::string& name) const {
    return AssetManager::Get().GetShader(name);
}

ShaderProgram* GraphicsSubsystem::GetShaderByID(uint32_t id) const {
    return AssetManager::Get().GetShaderByID(id);
}

MeshHandle GraphicsSubsystem::GetMesh(const std::string& name) const {
    return AssetManager::Get().GetMesh(name);
}


void GraphicsSubsystem::PushCanvasQuad(
  float x,
  float y,
  float w,
  float h,
  float angle,
  float pivotX,
  float pivotY,
  const glm::vec4& color,
  int texIndex,
  CanvasLayer layer,
  const glm::vec2& uvMin,
  const glm::vec2& uvMax
) {
    glm::vec2 pivotOffset = glm::vec2(w * pivotX, h * pivotY);
    // glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f));
    // transform = glm::rotate(transform, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    // transform = glm::scale(transform, glm::vec3(size, 1.0f));

    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(x + pivotOffset.x, y + pivotOffset.y, 0.0f));
    glm::mat4 R = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(w, h, 1.0f));
    glm::mat4 O = glm::translate(glm::mat4(1.0f), glm::vec3(-pivotX, -pivotY, 0.0f));
    glm::mat4 transform = T * R * S * O;

    glm::vec4 bl = transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 br = transform * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 tr = transform * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
    glm::vec4 tl = transform * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

    canvasDrawList.push_back({ glm::vec2(bl), glm::vec2(uvMin.x, uvMin.y), color, texIndex, layer });
    canvasDrawList.push_back({ glm::vec2(br), glm::vec2(uvMax.x, uvMin.y), color, texIndex, layer });
    canvasDrawList.push_back({ glm::vec2(tr), glm::vec2(uvMax.x, uvMax.y), color, texIndex, layer });
    canvasDrawList.push_back({ glm::vec2(bl), glm::vec2(uvMin.x, uvMin.y), color, texIndex, layer });
    canvasDrawList.push_back({ glm::vec2(tr), glm::vec2(uvMax.x, uvMax.y), color, texIndex, layer });
    canvasDrawList.push_back({ glm::vec2(tl), glm::vec2(uvMin.x, uvMax.y), color, texIndex, layer });
}

void GraphicsSubsystem::PushCanvasQuadTiled(
  float x,
  float y,
  float w,
  float h,
  float angle,
  float pivotX,
  float pivotY,
  const glm::vec4& color,
  int texIndex,
  CanvasLayer layer,
  const glm::vec2& tilesetSize,
  const glm::vec2& tileIndex
) {
    glm::vec2 uvMin = tileIndex / tilesetSize;
    glm::vec2 uvMax = (tileIndex + glm::vec2(1.0f)) / tilesetSize;
    PushCanvasQuad(x, y, w, h, angle, pivotX, pivotY, color, texIndex, layer, uvMin, uvMax);
}


MeshComponent* GraphicsSubsystem::RegisterMesh(MeshComponent* mesh) {
    renderables.push_back(mesh);
    return mesh;
}

CanvasDrawable* GraphicsSubsystem::RegisterCanvasDrawable(CanvasDrawable* drawable) {
    canvasDrawables.push_back(drawable);
    return drawable;
}

CameraComponent* GraphicsSubsystem::RegisterCamera(CameraComponent* camera) {
    cameras.push_back(camera);
    return camera;
}

LightComponent* GraphicsSubsystem::RegisterLight(LightComponent* light) {
    if (light->type == LightType::Point) {
        pointLights.push_back(light);
    } else if (light->type == LightType::Directional) {
        directionalLights.push_back(light);
    }
    return light;
}

SunComponent* GraphicsSubsystem::RegisterSun(SunComponent* sun) {
    sunComponents.push_back(sun);
    return sun;
}

void GraphicsSubsystem::UnregisterCamera(CameraComponent* camera) {
    auto it = std::find(cameras.begin(), cameras.end(), camera);
    if (it != cameras.end()) {
        cameras.erase(it);
    }
}

void GraphicsSubsystem::UnregisterLight(LightComponent* light) {
    if (light->type == LightType::Point) {
        auto it = std::find(pointLights.begin(), pointLights.end(), light);
        if (it != pointLights.end()) {
            pointLights.erase(it);
        }
    } else if (light->type == LightType::Directional) {
        auto it = std::find(directionalLights.begin(), directionalLights.end(), light);
        if (it != directionalLights.end()) {
            directionalLights.erase(it);
        }
    }
}

void GraphicsSubsystem::UnregisterMesh(MeshComponent* mesh) {
    auto it = std::find(renderables.begin(), renderables.end(), mesh);
    if (it != renderables.end()) {
        renderables.erase(it);
    }
}

void GraphicsSubsystem::UnregisterCanvasDrawable(CanvasDrawable* drawable) {
    auto it = std::find(canvasDrawables.begin(), canvasDrawables.end(), drawable);
    if (it != canvasDrawables.end()) {
        canvasDrawables.erase(it);
    }
}

// ===== Render Target Management Implementation =====
std::shared_ptr<RenderTarget> GraphicsSubsystem::CreateRenderTarget(int width, int height, bool withDepth) {
    RenderTarget::Props p;
    p.width = width;
    p.height = height;
    p.withDepth = withDepth;
    auto rt = std::shared_ptr<RenderTarget>(GfxFactory::CreateRenderTarget(p).release());
    _renderTargets.push_back(rt);
    return rt;
}

std::shared_ptr<RenderTarget> GraphicsSubsystem::CreateRenderTarget(const RenderTarget::Props& props) {
    auto rt = std::shared_ptr<RenderTarget>(GfxFactory::CreateRenderTarget(props).release());
    _renderTargets.push_back(rt);
    return rt;
}

void GraphicsSubsystem::PushRenderTarget(RenderTarget* target) {
    // Save current target to stack
    _renderTargetStack.push(_currentRenderTarget);

    // Activate new target
    if (target) {
        target->Begin();
    } else {
        // If switching to default framebuffer and we had a target, end it
        if (_currentRenderTarget) {
            _currentRenderTarget->End();
        }
    }

    _currentRenderTarget = target;
}

void GraphicsSubsystem::PopRenderTarget() {
    if (_renderTargetStack.empty()) {
        ConsoleSubsystem::Get()->Warn("GraphicsSubsystem::PopRenderTarget - Stack is empty!");
        return;
    }

    // End current target if any
    if (_currentRenderTarget) {
        _currentRenderTarget->End();
    }

    // Restore previous target
    RenderTarget* prevTarget = _renderTargetStack.top();
    _renderTargetStack.pop();

    if (prevTarget) {
        prevTarget->Begin();
    }

    _currentRenderTarget = prevTarget;
}

void GraphicsSubsystem::SetRenderTarget(RenderTarget* target) {
    // End current target if switching
    if (_currentRenderTarget && _currentRenderTarget != target) {
        _currentRenderTarget->End();
    }

    // Begin new target
    if (target && target != _currentRenderTarget) {
        target->Begin();
    } else if (!target && _currentRenderTarget) {
        // Switching to default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        auto [width, height] = Window::Get()->GetPhysicalSize();
        glViewport(0, 0, width, height);
    }

    _currentRenderTarget = target;
}

RenderTarget* GraphicsSubsystem::GetCurrentRenderTarget() const {
    return _currentRenderTarget;
}

RenderMeshHandle GraphicsSubsystem::AllocateRenderMesh(VertexFormat format, BufferUsage usage) {
    auto buf = GfxFactory::CreateBuffer();
    buf->Initialize(format, usage);

    RenderMeshHandle handle;
    handle.id = _nextRenderMeshId++;
    _renderMeshes[handle.id] = std::move(buf);

    return handle;
}

void GraphicsSubsystem::FreeRenderMesh(RenderMeshHandle handle) {
    if (!handle.IsValid()) return;

    auto it = _renderMeshes.find(handle.id);
    if (it != _renderMeshes.end()) {
        _renderMeshes.erase(it);
    }
}

Buffer* GraphicsSubsystem::GetRenderMesh(RenderMeshHandle handle) {
    if (!handle.IsValid()) return nullptr;

    auto it = _renderMeshes.find(handle.id);
    if (it != _renderMeshes.end()) {
        return it->second.get();
    }
    return nullptr;
}

// ===== 2D Rendering (Queued for UI) =====

static void CreateQuad(
  std::vector<BatchVertex>& vertices,
  std::vector<uint32_t>& indices,
  const glm::mat4& transform,
  const glm::vec4& color,
  const glm::vec2* uvs = nullptr
) {
    uint32_t startIndex = vertices.size();

    glm::vec2 defaultUVs[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    const glm::vec2* finalUVs = uvs ? uvs : defaultUVs;

    glm::vec4 positions[] = { { -0.5f, -0.5f, 0.0f, 1.0f },
                              { 0.5f, -0.5f, 0.0f, 1.0f },
                              { 0.5f, 0.5f, 0.0f, 1.0f },
                              { -0.5f, 0.5f, 0.0f, 1.0f } };

    for (int i = 0; i < 4; i++) {
        BatchVertex v;
        v.position = glm::vec3(transform * positions[i]);
        v.color = color;
        v.uv = finalUVs[i];
        v.texIndex = 0.0f;// Will be set by DrawGeometry based on textureID
        v.entityID = -1.0f;
        vertices.push_back(v);
    }

    indices.push_back(startIndex + 0);
    indices.push_back(startIndex + 1);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 3);
    indices.push_back(startIndex + 0);
}

void GraphicsSubsystem::DrawQuad(float x, float y, float w, float h, float rotation, const glm::vec4& color) {
    DrawTexturedQuad(x, y, w, h, rotation, 0, color);
}

void GraphicsSubsystem::DrawTexturedQuad(
  float x, float y, float w, float h, float rotation, uint32_t textureID, const glm::vec4& color
) {
    BatchDrawCommand cmd;
    cmd.textureID = textureID;
    cmd.transform = glm::mat4(1.0f);// Transform handled in vertices

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
    transform = glm::rotate(transform, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::scale(transform, glm::vec3(w, h, 1.0f));

    CreateQuad(cmd.vertices, cmd.indices, transform, color);
    renderer->SubmitCanvasCommand(cmd);
}

void GraphicsSubsystem::DrawRect(float x, float y, float w, float h, const glm::vec4& color) {
    // Draw 4 lines
    DrawLine(x, y, x + w, y, color);// Top
    DrawLine(x + w, y, x + w, y + h, color);// Right
    DrawLine(x + w, y + h, x, y + h, color);// Bottom
    DrawLine(x, y + h, x, y, color);// Left
}

void GraphicsSubsystem::DrawLine(float x1, float y1, float x2, float y2, const glm::vec4& color) {
    // Draw as a thin quad
    glm::vec2 p0(x1, y1);
    glm::vec2 p1(x2, y2);
    glm::vec2 dir = p1 - p0;
    float len = glm::length(dir);
    if (len < 0.001f) return;

    float angle = std::atan2(dir.y, dir.x);
    float cx = (x1 + x2) * 0.5f;
    float cy = (y1 + y2) * 0.5f;

    DrawQuad(cx, cy, len, 1.0f, angle, color);
}

void GraphicsSubsystem::DrawCircle(float x, float y, float radius, const glm::vec4& color) {
    const int segments = 32;
    float angleStep = 6.28318f / segments;
    for (int i = 0; i < segments; i++) {
        float a0 = i * angleStep;
        float a1 = (i + 1) * angleStep;
        DrawLine(
          x + std::cos(a0) * radius,
          y + std::sin(a0) * radius,
          x + std::cos(a1) * radius,
          y + std::sin(a1) * radius,
          color
        );
    }
}

// ===== Text Rendering Implementation =====

FontHandle GraphicsSubsystem::LoadFont(const std::string& path, float baseSize) {
    return _fontManager.LoadFont(path, baseSize);
}

void GraphicsSubsystem::UnloadFont(FontHandle id) {
    _fontManager.UnloadFont(id);
}

FontHandle GraphicsSubsystem::GetOrCreateDefaultFont() {
    if (_defaultFont == 0) {
        _defaultFont = LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 48.0f);
    }
    return _defaultFont;
}

float GraphicsSubsystem::GetFontBaseSize(FontHandle fontID) {
    if (auto* font = _fontManager.GetFont(fontID))
        return font->fontSize;
    return 48.0f;
}

void GraphicsSubsystem::DrawText(
  FontHandle fontID, const std::string& text, float x, float y, float scale, const glm::vec4& color
) {
    _textCommands.push_back({ fontID, text, x, y, scale, color });
}

void GraphicsSubsystem::RenderBufferedText(BatchRenderer2D* batch) {
    if (_textCommands.empty()) return;

    for (const auto& cmd : _textCommands) {
        Font* font = _fontManager.GetFont(cmd.fontID);
        if (!font) continue;

        float cursorX = cmd.x;

        for (char c : cmd.text) {
            const Glyph* glyph = _fontManager.GetGlyph(cmd.fontID, static_cast<int>(c));
            if (!glyph) continue;

            float drawX = cursorX + glyph->xOffset * cmd.scale;
            float drawY = cmd.y + (font->ascent + glyph->yOffset) * cmd.scale;
            float drawW = glyph->width * cmd.scale;
            float drawH = glyph->height * cmd.scale;

            if (drawW > 0 && drawH > 0) {
                // Adjust for centered quad rendering
                float finalX = drawX + drawW * 0.5f;
                float finalY = drawY + drawH * 0.5f;

                // Create UV coordinates for this glyph
                // Under Y-down: top is v0, bottom is v1 in atlas
                // Quad vertices are: 0=TL, 1=TR, 2=BR, 3=BL
                glm::vec2 uvs[4] = {
                    { glyph->u0, glyph->v0 },// top-left (v0)
                    { glyph->u1, glyph->v0 },// top-right (v0)
                    { glyph->u1, glyph->v1 },// bottom-right (v1)
                    { glyph->u0, glyph->v1 }// bottom-left (v1)
                };

                glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(finalX, finalY, 0.0f));
                transform = glm::scale(transform, glm::vec3(drawW, drawH, 1.0f));
                batch->DrawQuad(transform, font->textureID, uvs, cmd.color);
            }

            cursorX += glyph->advance * cmd.scale;
        }
    }
    _textCommands.clear();
}

void GraphicsSubsystem::FlushTextToQueue() {
    for (const auto& cmd : _textCommands) {
        Font* font = _fontManager.GetFont(cmd.fontID);
        if (!font) continue;
        float cursorX = cmd.x;
        for (char c : cmd.text) {
            const Glyph* glyph = _fontManager.GetGlyph(cmd.fontID, static_cast<int>(c));
            if (!glyph) continue;
            float drawX = cursorX + glyph->xOffset * cmd.scale;
            float drawY = cmd.y + glyph->yOffset * cmd.scale + font->ascent * cmd.scale;
            float drawW = glyph->width  * cmd.scale;
            float drawH = glyph->height * cmd.scale;
            if (drawW > 0 && drawH > 0) {
                float finalX = drawX + drawW * 0.5f;
                float finalY = drawY + drawH * 0.5f;
                glm::vec2 uvs[4] = {
                    { glyph->u0, glyph->v0 },
                    { glyph->u1, glyph->v0 },
                    { glyph->u1, glyph->v1 },
                    { glyph->u0, glyph->v1 },
                };
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(finalX, finalY, 0.0f));
                transform = glm::scale(transform, glm::vec3(drawW, drawH, 1.0f));
                BatchDrawCommand bdc;
                bdc.textureID = font->textureID;
                bdc.transform = glm::mat4(1.0f);
                CreateQuad(bdc.vertices, bdc.indices, transform, cmd.color, uvs);
                renderer->SubmitCanvasCommand(bdc);
            }
            cursorX += glyph->advance * cmd.scale;
        }
    }
    _textCommands.clear();
}

glm::vec2 GraphicsSubsystem::MeasureText(FontHandle fontID, const std::string& text, float scale) {
    return _fontManager.MeasureText(fontID, text, scale);
}

float GraphicsSubsystem::GetFontLineHeight(FontHandle fontID, float scale) {
    Font* font = _fontManager.GetFont(fontID);
    return font ? font->lineHeight * scale : 0.0f;
}

// Draw text at 3D position
void GraphicsSubsystem::DrawText3D(
  FontHandle fontID, const std::string& text, glm::vec3 position, float scale, const glm::vec4& color
) {
    auto* camera = GetMainCamera();
    if (!camera) return;

    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 projection = camera->GetProjectionMatrix();
    glm::mat4 viewProjection = projection * view;

    glm::vec4 clipSpacePos = viewProjection * glm::vec4(position, 1.0f);

    // Check if behind camera
    if (clipSpacePos.w <= 0.0f) {
        return;
    }

    // Perspective division
    glm::vec3 ndc = glm::vec3(clipSpacePos) / clipSpacePos.w;

    // Viewport transform — use logical size to match UIPass projection space
    auto [width, height] = Window::Get()->GetLogicalSize();
    float x = (ndc.x + 1.0f) * 0.5f * width;
    float y = (1.0f - ndc.y) * 0.5f * height; // Y-Down: match UIPass coordinate space

    DrawText(fontID, text, x, y, scale, color);
}