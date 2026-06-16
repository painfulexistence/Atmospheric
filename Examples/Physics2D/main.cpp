#include "Atmospheric.hpp"
#include "components.hpp"
#include <random>

// Each falling body owns its hit-flash recovery via ColorRestoreComponent (components.hpp), so
// the per-body fade loop disappears from OnUpdate. Spawning stays in OnUpdate
// because the engine ticks entities with a live iterator — creating GameObjects
// must happen outside the component tick (here, or in OnLoad/OnAttach).
class Physics2DDemo : public Application {
    using Application::Application;

    std::mt19937 rng;
    float spawnTimer = 0.0f;
    int spawnedCount = 0;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        rng.seed(42);
        spawnTimer = 0.0f;
        spawnedCount = 0;

        mainCamera = graphics.GetMainCamera();

        // Set up orthographic camera for 2D view
        if (mainCamera) {
            mainCamera->SetOrthographic(800.0f, 600.0f, -100.0f, 100.0f);
            mainCamera->gameObject->SetPosition(glm::vec3(400.0f, 300.0f, 0.0f));
            // Default camera looks at +X (0 angle). Rotate -90 degrees to look at -Z.
            mainCamera->Yaw(-glm::half_pi<float>());
        }

        // Set gravity (positive Y = up in math/GL coords, so we need negative for down)
        physics2D.SetGravity(glm::vec2(0.0f, -500.0f));

        // Static world geometry
        CreateGround();
        CreateWalls();

        // Flash a body white when it starts touching another. Its
        // ColorRestoreComponent eases it back to its rest colour over time.
        physics2D.SetBeginContactCallback([](Rigidbody2DComponent* a, Rigidbody2DComponent* b) {
            if (a->gameObject) {
                if (auto* shape = a->gameObject->GetComponent<ShapeRendererComponent>()) {
                    shape->SetColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                }
            }
        });

        // HUD
        auto* textObj = CreateGameObject(glm::vec2(20.0f, 100.0f));
        textObj->SetName("Physics2D Demo");
        textObj->AddComponent<TextComponent>(TextProps{
          .text = "Physics2D Demo - Press SPACE to spawn shapes, R to reset",
          .fontSize = 64.0f,
          .color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
          .layer = CanvasLayer::LAYER_WORLD_2D,
        });
    }

    void CreateGround() {
        auto* ground = CreateGameObject(glm::vec2(400.0f, 50.0f));

        ShapeRendererProps shapeProps;
        shapeProps.type = ShapeType2D::Box;
        shapeProps.boxHalfSize = glm::vec2(350.0f, 15.0f);
        shapeProps.color = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
        shapeProps.filled = true;
        ground->AddComponent<ShapeRendererComponent>(shapeProps);
        ground->AddComponent<ColorRestoreComponent>();

        Rigidbody2DProps rbProps;
        rbProps.type = BodyType2D::Static;
        rbProps.shape.type = ShapeType2D::Box;
        rbProps.shape.boxSize = glm::vec2(700.0f, 30.0f);
        ground->AddComponent<Rigidbody2DComponent>(rbProps);
    }

    void CreateWalls() {
        auto makeWall = [this](glm::vec2 pos) {
            auto* wall = CreateGameObject(pos);

            ShapeRendererProps shapeProps;
            shapeProps.type = ShapeType2D::Box;
            shapeProps.boxHalfSize = glm::vec2(10.0f, 250.0f);
            shapeProps.color = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
            shapeProps.filled = true;
            wall->AddComponent<ShapeRendererComponent>(shapeProps);
            wall->AddComponent<ColorRestoreComponent>();

            Rigidbody2DProps rbProps;
            rbProps.type = BodyType2D::Static;
            rbProps.shape.type = ShapeType2D::Box;
            rbProps.shape.boxSize = glm::vec2(20.0f, 500.0f);
            wall->AddComponent<Rigidbody2DComponent>(rbProps);
        };

        makeWall(glm::vec2(30.0f, 300.0f));  // Left wall
        makeWall(glm::vec2(770.0f, 300.0f)); // Right wall
    }

    void SpawnRandomShape(glm::vec2 position) {
        std::uniform_int_distribution<int> shapeDist(0, 2);
        std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
        std::uniform_real_distribution<float> sizeDist(20.0f, 50.0f);

        int shapeType = shapeDist(rng);
        glm::vec4 color(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);

        auto* body = CreateGameObject(position);

        Rigidbody2DProps rbProps;
        rbProps.type = BodyType2D::Dynamic;
        rbProps.shape.density = 1.0f;
        rbProps.shape.friction = 0.3f;
        rbProps.shape.restitution = 0.4f;

        ShapeRendererProps shapeProps;
        shapeProps.color = color;
        shapeProps.filled = true;

        switch (shapeType) {
        case 0: {
            float size = sizeDist(rng);
            shapeProps.type = ShapeType2D::Box;
            shapeProps.boxHalfSize = glm::vec2(size * 0.5f, size * 0.5f);

            rbProps.shape.type = ShapeType2D::Box;
            rbProps.shape.boxSize = glm::vec2(size, size);
            break;
        }
        case 1: {
            float radius = sizeDist(rng) * 0.5f;
            shapeProps.type = ShapeType2D::Circle;
            shapeProps.radius = radius;

            rbProps.shape.type = ShapeType2D::Circle;
            rbProps.shape.circleRadius = radius;
            break;
        }
        case 2: {
            std::uniform_int_distribution<int> vertDist(3, 5);
            int numVerts = vertDist(rng);
            float size = sizeDist(rng);

            std::vector<glm::vec2> vertices;
            for (int i = 0; i < numVerts; ++i) {
                float angle = (2.0f * glm::pi<float>() * i) / numVerts - glm::pi<float>() / 2.0f;
                vertices.push_back(glm::vec2(std::cos(angle) * size * 0.5f, std::sin(angle) * size * 0.5f));
            }
            shapeProps.type = ShapeType2D::Polygon;
            shapeProps.vertices = vertices;

            rbProps.shape.type = ShapeType2D::Polygon;
            rbProps.shape.polygonVertices = vertices;
            break;
        }
        }

        body->AddComponent<ShapeRendererComponent>(shapeProps);
        body->AddComponent<Rigidbody2DComponent>(rbProps);
        body->AddComponent<ColorRestoreComponent>(2.0f, glm::vec4(0.7f, 0.5f, 0.8f, 1.0f));

        spawnedCount++;
    }

    void OnUpdate(float dt, float time) override {
        // Spawn shapes on space press
        if (input.IsKeyPressed(Key::SPACE)) {
            std::uniform_real_distribution<float> xDist(100.0f, 700.0f);
            SpawnRandomShape(glm::vec2(xDist(rng), 550.0f));
        }

        // Auto-spawn every second up to a cap
        spawnTimer += dt;
        if (spawnTimer > 1.0f && spawnedCount < 50) {
            std::uniform_real_distribution<float> xDist(100.0f, 700.0f);
            SpawnRandomShape(glm::vec2(xDist(rng), 550.0f));
            spawnTimer = 0.0f;
        }

        // Reset scene
        if (input.IsKeyPressed(Key::R)) {
            ReloadScene();
        }
        if (input.IsKeyDown(Key::ESCAPE)) {
            Quit();
        }
    }
};

int main(int argc, char* argv[]) {
    Physics2DDemo game({
      .windowTitle = "Physics2D Demo - Polygon Collision",
      .windowWidth = 800,
      .windowHeight = 600,
      .useDefaultTextures = true,
      .useDefaultShaders = true,
    });
    game.Run();
    return 0;
}
