#include "editor_layer.hpp"
#include "application.hpp"
#include "component.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "imgui.h"
#include "video_recorder.hpp"
#include "window.hpp"
#include "gfx_factory.hpp"
#include <ctime>
#include <filesystem>

EditorLayer::EditorLayer(Application* app, bool showImGui)
    : Layer("EditorLayer"), _app(app), _showImGui(showImGui) {
}

void EditorLayer::OnUpdate(float dt) {
#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
    if (ImGui::IsKeyPressed(ImGuiKey_F1))
        _showImGui = !_showImGui;
}

void EditorLayer::ToggleRecording() {
    auto* recorder = _app->GetRecorder();
    if (recorder->isRecording()) {
        recorder->stopRecording();
    } else {
        std::error_code ec;
        std::filesystem::create_directories("output", ec);
        std::time_t t = std::time(nullptr);
        char name[80];
        std::strftime(name, sizeof(name), "output/recording_%Y%m%d_%H%M%S.mp4",
                      std::localtime(&t));
        VideoRecorder::Config cfg;
        cfg.outputPath = name;
        recorder->startRecording(_app->GetGraphicsServer()->renderer, cfg);
    }
}

void EditorLayer::OnRender(float dt) {
#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
    // Handled on the render thread (same as VideoRecorder::captureFrame) and
    // before the visibility check, so F2 works even when the overlay is hidden.
    if (ImGui::IsKeyPressed(ImGuiKey_F2))
        ToggleRecording();

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
    for (auto* comp : entity->GetComponents()) {
        if (ImGui::CollapsingHeader(comp->GetName().c_str()))
            comp->DrawImGui();
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
        if (ImGui::CollapsingHeader("Recording (F2)")) {
            _app->GetRecorder()->drawImGui(*_app->GetGraphicsServer()->renderer);
        }
#endif
    }
    ImGui::End();
}
