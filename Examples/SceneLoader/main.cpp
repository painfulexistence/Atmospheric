#include "Atmospheric.hpp"

class CSBDemo : public Application {
    using Application::Application;

    SceneLoader* sceneLoader = nullptr;
    SceneLoadResult loadedScene;
    bool showDebugGrid = false;
    bool showNodeInfo = false;

    // Test sprites for layout verification
    std::vector<GameObject*> testSprites;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        // Initialize scene loader
        sceneLoader = new SceneLoader(this);

        // Set up orthographic camera for 2D view
        mainCamera = graphics.GetMainCamera();
        if (mainCamera) {
            mainCamera->SetOrthographic(800.0f, 600.0f, -100.0f, 100.0f);
            mainCamera->gameObject->SetPosition(glm::vec3(400.0f, 300.0f, 0.0f));
            mainCamera->Yaw(-glm::half_pi<float>());
        }

        loadedScene = sceneLoader->Load("assets/scenes/Canvas.csb", glm::vec3(0.0f), CanvasLayer::LAYER_WORLD);
        if (loadedScene.success) {
            console.Info(fmt::format("CSB loaded successfully! {} nodes created", loadedScene.allNodes.size()));
        } else {
            console.Warn(fmt::format("CSB load failed: {}", loadedScene.error));
            console.Info("Creating layout test sprites...");
            CreateLayoutTestSprites();
        }

        console.Info("=== CSB Demo Controls ===");
        console.Info("1 - Toggle debug grid/coordinate system");
        console.Info("2 - Toggle node info overlay");
        console.Info("R - Reload scene");
        console.Info("ESC - Quit");
    }

    void CreateLayoutTestSprites() {
        // Create test sprites specifically for verifying layout correctness
        // These test: position, size, pivot, scale, rotation, flip, zOrder

        // === Test 1: Origin marker (should be at 0,0) ===
        auto origin = CreateGameObject(glm::vec2(0.0f, 0.0f));
        SpriteProps originProps;
        originProps.size = glm::vec2(20.0f, 20.0f);
        originProps.pivot = glm::vec2(0.5f, 0.5f);
        originProps.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);// Red
        originProps.layer = CanvasLayer::LAYER_OVERLAY;
        origin->AddComponent<SpriteComponent>(originProps);
        origin->SetName("Origin (0,0)");
        testSprites.push_back(origin);

        // === Test 2: Different pivot points ===
        // All at same position (200, 300) but different pivots
        float testX = 200.0f;
        float testY = 300.0f;

        // Pivot (0,0) - bottom-left
        auto pivotBL = CreateGameObject(glm::vec2(testX, testY));
        SpriteProps blProps;
        blProps.size = glm::vec2(80.0f, 60.0f);
        blProps.pivot = glm::vec2(0.0f, 0.0f);// Bottom-left
        blProps.color = glm::vec4(1.0f, 0.5f, 0.0f, 0.7f);// Orange
        blProps.layer = CanvasLayer::LAYER_WORLD;
        blProps.zOrder = 1;
        pivotBL->AddComponent<SpriteComponent>(blProps);
        pivotBL->SetName("Pivot(0,0) BL");
        testSprites.push_back(pivotBL);

        // Pivot (0.5, 0.5) - center
        auto pivotC = CreateGameObject(glm::vec2(testX, testY));
        SpriteProps cProps;
        cProps.size = glm::vec2(80.0f, 60.0f);
        cProps.pivot = glm::vec2(0.5f, 0.5f);// Center
        cProps.color = glm::vec4(0.0f, 1.0f, 0.5f, 0.7f);// Green
        cProps.layer = CanvasLayer::LAYER_WORLD;
        cProps.zOrder = 2;
        pivotC->AddComponent<SpriteComponent>(cProps);
        pivotC->SetName("Pivot(0.5,0.5) C");
        testSprites.push_back(pivotC);

        // Pivot (1,1) - top-right
        auto pivotTR = CreateGameObject(glm::vec2(testX, testY));
        SpriteProps trProps;
        trProps.size = glm::vec2(80.0f, 60.0f);
        trProps.pivot = glm::vec2(1.0f, 1.0f);// Top-right
        trProps.color = glm::vec4(0.0f, 0.5f, 1.0f, 0.7f);// Blue
        trProps.layer = CanvasLayer::LAYER_WORLD;
        trProps.zOrder = 3;
        pivotTR->AddComponent<SpriteComponent>(trProps);
        pivotTR->SetName("Pivot(1,1) TR");
        testSprites.push_back(pivotTR);

        // === Test 3: Scale test ===
        auto scaled = CreateGameObject(glm::vec2(400.0f, 300.0f));
        scaled->SetScale(glm::vec3(2.0f, 1.5f, 1.0f));// 2x width, 1.5x height
        SpriteProps scaledProps;
        scaledProps.size = glm::vec2(50.0f, 50.0f);// Base size 50x50
        scaledProps.pivot = glm::vec2(0.5f, 0.5f);
        scaledProps.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.8f);// Yellow
        scaledProps.layer = CanvasLayer::LAYER_WORLD;
        scaled->AddComponent<SpriteComponent>(scaledProps);
        scaled->SetName("Scaled 2x1.5 (100x75)");
        testSprites.push_back(scaled);

        // === Test 4: Rotation test ===
        auto rotated = CreateGameObject(glm::vec2(550.0f, 300.0f));
        rotated->SetRotation(glm::vec3(0.0f, 0.0f, glm::radians(45.0f)));// 45 degrees
        SpriteProps rotProps;
        rotProps.size = glm::vec2(60.0f, 40.0f);
        rotProps.pivot = glm::vec2(0.5f, 0.5f);
        rotProps.color = glm::vec4(1.0f, 0.0f, 1.0f, 0.8f);// Magenta
        rotProps.layer = CanvasLayer::LAYER_WORLD;
        rotated->AddComponent<SpriteComponent>(rotProps);
        rotated->SetName("Rotated 45deg");
        testSprites.push_back(rotated);

        // === Test 5: Flip test ===
        // Normal
        auto normal = CreateGameObject(glm::vec2(650.0f, 450.0f));
        SpriteProps normalProps;
        normalProps.size = glm::vec2(60.0f, 40.0f);
        normalProps.pivot = glm::vec2(0.5f, 0.5f);
        normalProps.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);// Cyan
        normalProps.layer = CanvasLayer::LAYER_WORLD;
        normal->AddComponent<SpriteComponent>(normalProps);
        normal->SetName("Normal");
        testSprites.push_back(normal);

        // FlipX
        auto flipX = CreateGameObject(glm::vec2(650.0f, 500.0f));
        SpriteProps flipXProps;
        flipXProps.size = glm::vec2(60.0f, 40.0f);
        flipXProps.pivot = glm::vec2(0.5f, 0.5f);
        flipXProps.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
        flipXProps.flipX = true;
        flipXProps.layer = CanvasLayer::LAYER_WORLD;
        flipX->AddComponent<SpriteComponent>(flipXProps);
        flipX->SetName("FlipX");
        testSprites.push_back(flipX);

        // FlipY
        auto flipY = CreateGameObject(glm::vec2(650.0f, 550.0f));
        SpriteProps flipYProps;
        flipYProps.size = glm::vec2(60.0f, 40.0f);
        flipYProps.pivot = glm::vec2(0.5f, 0.5f);
        flipYProps.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
        flipYProps.flipY = true;
        flipYProps.layer = CanvasLayer::LAYER_WORLD;
        flipY->AddComponent<SpriteComponent>(flipYProps);
        flipY->SetName("FlipY");
        testSprites.push_back(flipY);

        // === Test 6: Aspect ratio test (should be 16:9) ===
        auto aspect = CreateGameObject(glm::vec2(400.0f, 500.0f));
        SpriteProps aspectProps;
        aspectProps.size = glm::vec2(160.0f, 90.0f);// 16:9 ratio
        aspectProps.pivot = glm::vec2(0.5f, 0.5f);
        aspectProps.color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);// Light gray
        aspectProps.layer = CanvasLayer::LAYER_WORLD_BACK;
        aspect->AddComponent<SpriteComponent>(aspectProps);
        aspect->SetName("16:9 Aspect (160x90)");
        testSprites.push_back(aspect);

        // === Test 7: zOrder test (stacked at same position) ===
        for (int i = 0; i < 3; i++) {
            auto stacked = CreateGameObject(glm::vec2(100.0f + i * 15.0f, 500.0f + i * 15.0f));
            SpriteProps stackProps;
            stackProps.size = glm::vec2(50.0f, 50.0f);
            stackProps.pivot = glm::vec2(0.5f, 0.5f);
            stackProps.color = glm::vec4(i == 0 ? 1.0f : 0.3f, i == 1 ? 1.0f : 0.3f, i == 2 ? 1.0f : 0.3f, 0.9f);
            stackProps.layer = CanvasLayer::LAYER_WORLD;
            stackProps.zOrder = i;// 0, 1, 2
            stacked->AddComponent<SpriteComponent>(stackProps);
            stacked->SetName(fmt::format("zOrder={}", i));
            testSprites.push_back(stacked);
        }

        console.Info(fmt::format("Created {} layout test sprites", testSprites.size()));
    }

    void OnUpdate(float dt, float time) override {
        // Toggle debug grid
        if (input.IsKeyPressed(Key::Num1)) {
            showDebugGrid = !showDebugGrid;
            console.Info(fmt::format("Debug grid: {}", showDebugGrid ? "ON" : "OFF"));
        }

        // Toggle node info
        if (input.IsKeyPressed(Key::Num2)) {
            showNodeInfo = !showNodeInfo;
            console.Info(fmt::format("Node info: {}", showNodeInfo ? "ON" : "OFF"));
        }

        // Reload scene
        if (input.IsKeyPressed(Key::R)) {
            ReloadScene();
        }

        if (input.IsKeyDown(Key::ESCAPE)) {
            Quit();
        }

        // Draw debug overlay
        if (showDebugGrid) {
            DrawDebugGrid();
        }

        if (showNodeInfo) {
            DrawNodeInfo();
        }
    }

    void DrawDebugGrid() {
        // Draw using ImGui overlay
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::Begin(
          "DebugOverlay",
          nullptr,
          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs
        );

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Colors
        ImU32 gridColor = IM_COL32(100, 100, 100, 100);
        ImU32 axisXColor = IM_COL32(255, 100, 100, 200);
        ImU32 axisYColor = IM_COL32(100, 255, 100, 200);
        ImU32 textColor = IM_COL32(255, 255, 255, 200);

        // Note: ImGui Y is inverted (0 at top), but our coordinate system has 0 at bottom
        // We need to flip Y: screenY = 600 - worldY

        // Draw grid lines every 100 pixels
        for (int x = 0; x <= 800; x += 100) {
            drawList->AddLine(ImVec2(x, 0), ImVec2(x, 600), gridColor);
            // Label
            char label[16];
            snprintf(label, sizeof(label), "%d", x);
            drawList->AddText(ImVec2(x + 2, 600 - 15), textColor, label);
        }

        for (int y = 0; y <= 600; y += 100) {
            int screenY = 600 - y;// Flip Y
            drawList->AddLine(ImVec2(0, screenY), ImVec2(800, screenY), gridColor);
            // Label
            char label[16];
            snprintf(label, sizeof(label), "%d", y);
            drawList->AddText(ImVec2(2, screenY + 2), textColor, label);
        }

        // Draw X axis (Y=0, red)
        int y0Screen = 600;// Y=0 in world = bottom of screen
        drawList->AddLine(ImVec2(0, y0Screen), ImVec2(800, y0Screen), axisXColor, 2.0f);
        drawList->AddText(ImVec2(780, y0Screen - 15), axisXColor, "X");

        // Draw Y axis (X=0, green)
        drawList->AddLine(ImVec2(0, 0), ImVec2(0, 600), axisYColor, 2.0f);
        drawList->AddText(ImVec2(5, 5), axisYColor, "Y");

        // Draw origin marker
        drawList->AddCircleFilled(ImVec2(0, 600), 5, IM_COL32(255, 255, 0, 255));
        drawList->AddText(ImVec2(8, 600 - 18), IM_COL32(255, 255, 0, 255), "Origin(0,0)");

        ImGui::End();
    }

    void DrawNodeInfo() {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(250, 400));
        ImGui::Begin("Node Info", &showNodeInfo);

        ImGui::Text("Test Sprites:");
        ImGui::Separator();

        std::vector<GameObject*> allDebugNodes = testSprites;
        allDebugNodes.insert(allDebugNodes.end(), loadedScene.allNodes.begin(), loadedScene.allNodes.end());

        for (auto* go : allDebugNodes) {
            if (!go) continue;

            ImGui::PushID(go);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", go->GetName().c_str());

            glm::vec3 pos = go->GetPosition();
            ImGui::Text("  Pos: (%.0f, %.0f)", pos.x, pos.y);

            if (auto* sprite = go->GetComponent<SpriteComponent>()) {
                glm::vec3 scale = go->GetScale();
                glm::vec2 size = sprite->GetSize();
                glm::vec2 pivot = sprite->GetPivot();

                ImGui::Text("  [Sprite]");
                ImGui::Text("  Size: %.0fx%.0f", size.x, size.y);
                ImGui::Text("  Scale: %.1fx%.1f", scale.x, scale.y);
                ImGui::Text("  Pivot: (%.1f, %.1f)", pivot.x, pivot.y);
                ImGui::Text("  zOrder: %d", sprite->GetZOrder());
            }

            if (auto* text = go->GetComponent<TextComponent>()) {
                ImGui::Text("  [Text]");
                ImGui::Text("  Content: %s", text->GetText().c_str());
                ImGui::Text("  Font: %s", text->GetFontPath().c_str());
                ImGui::Text("  Size: %.1f", text->GetFontSize());
                auto col = text->GetColor();
                ImGui::Text("  Color: (%.2f, %.2f, %.2f, %.2f)", col.r, col.g, col.b, col.a);
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::End();
    }
};

int main(int argc, char* argv[]) {
    CSBDemo game({
      .windowTitle = "CSB Layout Test",
      .windowWidth = 800,
      .windowHeight = 600,
      .useDefaultTextures = true,
      .useDefaultShaders = true,
    });
    game.Run();
    return 0;
}
