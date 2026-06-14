#include "editor_layer.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "imgui.h"
#include "light_component.hpp"
#include "mesh_component.hpp"
#include "rigidbody_component.hpp"
#include "sprite_component.hpp"
#include "text_component.hpp"
#include "window.hpp"

EditorLayer::EditorLayer(Application* app, bool showImGui)
    : Layer("EditorLayer"), _app(app), _showImGui(showImGui) {
}

void EditorLayer::OnUpdate(float dt) {
    if (ImGui::IsKeyPressed(ImGuiKey_F1))
        _showImGui = !_showImGui;
}

void EditorLayer::OnRender(float dt) {
    if (!_showImGui) return;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("System Info", nullptr, &_showSystemInfo);
            ImGui::MenuItem("Engine", nullptr, &_showEngineView);
            ImGui::MenuItem("Application", nullptr, &_showAppView);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (_showSystemInfo) {
        DrawSystemInfo();
    }

    if (_showAppView) {
        DrawAppView();
    }

    if (_showEngineView) {
        DrawEngineView();
    }
}

void EditorLayer::DrawSystemInfo() {
    ImGui::Begin("System Information");
    {
        ImGui::Text("OpenGL: %s", glGetString(GL_VERSION));
        ImGui::Text("GLSL: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
        ImGui::Text("Vendor: %s", glGetString(GL_VENDOR));
        ImGui::Text("Renderer: %s", glGetString(GL_RENDERER));

        auto window = _app->GetWindow();
        auto [wx, wy] = window->GetSize();
        ImGui::Text("Window size: %dx%d", wx, wy);
        auto [fx, fy] = window->GetFramebufferSize();
        ImGui::Text("Framebuffer size: %dx%d", fx, fy);

        GLint depth, stencil;
        glGetFramebufferAttachmentParameteriv(
          GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth
        );
        glGetFramebufferAttachmentParameteriv(
          GL_DRAW_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencil
        );
        ImGui::Text("Depth bits: %d", depth);
        ImGui::Text("Stencil bits: %d", stencil);

        GLint maxVertUniforms, maxFragUniforms;
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxVertUniforms);
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &maxFragUniforms);
        ImGui::Text("Max vertex uniforms: %d bytes", maxVertUniforms / 4);
        ImGui::Text("Max fragment uniforms: %d bytes", maxFragUniforms / 4);

        GLint maxVertUniBlocks, maxFragUniBlocks;
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &maxVertUniBlocks);
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &maxFragUniBlocks);
        ImGui::Text("Max vertex uniform blocks: %d", maxVertUniBlocks);
        ImGui::Text("Max fragment uniform blocks: %d", maxFragUniBlocks);

        GLint maxElementIndices, maxElementVertices;
        glGetIntegerv(GL_MAX_ELEMENTS_INDICES, &maxElementIndices);
        glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &maxElementVertices);
        ImGui::Text("Max element indices: %d", maxElementIndices);
        ImGui::Text("Max element vertices: %d", maxElementVertices);
    }
    ImGui::End();
}

void EditorLayer::DrawAppView() {
    ImGui::Begin("Application");
    {
        ImGui::BeginChild("Scene", ImVec2(200, 400), true);
        ImGui::Text("Scene (%d entities)", (uint32_t)_app->GetEntities().size());
        if (ImGui::Button("Reload Scene")) {
            _app->ReloadScene();
        }
        ImGui::Separator();
        ImGui::BeginGroup();
        for (auto& entity : _app->GetEntities()) {
            bool selected = entity == _selectedEntity;
            if (ImGui::Selectable(entity->GetName().c_str(), selected)) {
                _selectedEntity = entity;
            }
        }
        ImGui::EndGroup();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("Entity", ImVec2(300, 400), true);
        ImGui::Text("Entity");
        ImGui::Separator();
        if (_selectedEntity) {
            DrawEntityInspector(_selectedEntity);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorLayer::DrawEntityInspector(GameObject* entity) {
    ImGui::Text("Name: %s", entity->GetName().c_str());

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 pos = entity->GetPosition();
        glm::vec3 rot = entity->GetRotation();
        glm::vec3 scale = entity->GetScale();
        if (ImGui::DragFloat3("Position", &pos.x, 0.1f)) entity->SetPosition(pos);
        if (ImGui::DragFloat3("Rotation", &rot.x, 1.0f)) entity->SetRotation(rot);
        if (ImGui::DragFloat3("Scale", &scale.x, 0.1f)) entity->SetScale(scale);
    }

    auto impostor = entity->GetComponent<RigidbodyComponent>();
    if (impostor != nullptr) {
        if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 vel = impostor->GetLinearVelocity();
            ImGui::Text("Velocity: %.3f, %.3f, %.3f", vel.x, vel.y, vel.z);
        }
    }

    auto light = entity->GetComponent<LightComponent>();
    if (light != nullptr) {
        if (light->type == LightType::Directional) {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("direction", &light->direction.x);
                ImGui::ColorEdit3("diffuse", &light->diffuse.r);
                ImGui::ColorEdit3("spcular", &light->specular.r);
                ImGui::ColorEdit3("ambient", &light->ambient.r);
                ImGui::DragFloat("intensity", &light->intensity);
                ImGui::Checkbox("castShadow", &light->castShadow);
            }
        } else if (light->type == LightType::Point) {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("attenuation", &light->attenuation.x);
                ImGui::ColorEdit3("diffuse", &light->diffuse.r);
                ImGui::ColorEdit3("spcular", &light->specular.r);
                ImGui::ColorEdit3("ambient", &light->ambient.r);
                ImGui::DragFloat("intensity", &light->intensity);
                ImGui::Checkbox("castShadow", &light->castShadow);
            }
        }
    }

    auto camera = entity->GetComponent<CameraComponent>();
    if (camera != nullptr) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        }
    }

    auto renderable = entity->GetComponent<MeshComponent>();
    if (renderable != nullptr) {
        if (ImGui::CollapsingHeader("MeshComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto mat = renderable->GetMaterial();
            auto graphics = _app->GetGraphicsServer();
            auto& assetManager = AssetManager::Get();
            int textureCount = assetManager.GetTextures().size();
            ImGui::SliderInt("Base map ID", &mat->baseMap, -1, textureCount - 1);
            ImGui::SliderInt("Normal map ID", &mat->normalMap, -1, textureCount - 1);
            ImGui::SliderInt("AO map ID", &mat->aoMap, -1, textureCount - 1);
            ImGui::SliderInt("Roughness map ID", &mat->roughnessMap, -1, textureCount - 1);
            ImGui::SliderInt("Metallic map ID", &mat->metallicMap, -1, textureCount - 1);
            ImGui::SliderInt("Height map ID", &mat->heightMap, -1, textureCount - 1);
            ImGui::ColorEdit3("Diffuse", &mat->diffuse.r);
            ImGui::ColorEdit3("Specular", &mat->specular.r);
            ImGui::ColorEdit3("Ambient", &mat->ambient.r);
            ImGui::DragFloat("Shininess", &mat->shininess, 0.0f, 1.0f);
            ImGui::Checkbox("Cull face enabled", &mat->cullFaceEnabled);
        }
    }

    auto drawable2D = entity->GetComponent<SpriteComponent>();
    if (drawable2D != nullptr) {
        if (ImGui::CollapsingHeader("SpriteComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec2 size = drawable2D->GetSize();
            glm::vec2 pivot = drawable2D->GetPivot();
            glm::vec4 color = drawable2D->GetColor();
            uint8_t textureID = drawable2D->GetTextureID();
            if (ImGui::DragFloat2("Size:", &size.x, 0.001f, 9999.999f)) {
                drawable2D->SetSize(size);
            }
            if (ImGui::DragFloat2("Pivot:", &pivot.x, 0.0f, 1.0f)) {
                drawable2D->SetPivot(pivot);
            }
            if (ImGui::ColorEdit4("Color", &color.r)) {
                drawable2D->SetColor(color);
            }
            auto graphics = _app->GetGraphicsServer();
            uint8_t minTexIndex = 0, maxTexIndex = graphics->canvasTextures.size() - 1;
            if (ImGui::SliderScalar("Texture ID", ImGuiDataType_U8, &textureID, &minTexIndex, &maxTexIndex)) {
                drawable2D->SetTextureID(textureID);
            }
        }
    }
    auto textComp = entity->GetComponent<TextComponent>();
    if (textComp != nullptr) {
        if (ImGui::CollapsingHeader("TextComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Text Content
            std::string text = textComp->GetText();
            char buffer[256];
            strncpy(buffer, text.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = 0;
            if (ImGui::InputText("Text", buffer, sizeof(buffer))) {
                textComp->SetText(std::string(buffer));
            }

            // Font Info
            ImGui::Text("Font Path: %s", textComp->GetFontPath().c_str());
            ImGui::Text("Font ID: %d", textComp->GetFontID());

            // Font Size
            float fontSize = textComp->GetFontSize();
            if (ImGui::DragFloat("Font Size", &fontSize, 1.0f, 1.0f, 200.0f)) {
                textComp->SetFontSize(fontSize);
            }

            // Color
            glm::vec4 color = textComp->GetColor();
            if (ImGui::ColorEdit4("Color", &color.r)) {
                textComp->SetColor(color);
            }
        }
    }
}

void EditorLayer::DrawEngineView() {
    ImGui::Begin("Engine Subsystems");
    {
        float dt = 1.0f / ImGui::GetIO().Framerate;
        _app->GetConsole()->DrawImGui(dt);
        _app->GetInput()->DrawImGui(dt);
        _app->GetGraphicsServer()->DrawImGui(dt);
        _app->GetPhysicsServer()->DrawImGui(dt);
#ifndef __EMSCRIPTEN__
        _app->GetAudioManager()->DrawImGui(dt);
#endif
    }
    ImGui::End();
}
