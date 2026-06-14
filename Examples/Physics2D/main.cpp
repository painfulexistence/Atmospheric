#include "Atmospheric.hpp"
#include "Atmospheric/shape_renderer_component.hpp"
#include <random>

class Physics2DDemo : public Application {
    using Application::Application;

    std::vector<GameObject*> dynamicBodies;
    GameObject* ground;
    std::vector<GameObject*> walls;

    std::mt19937 rng;
    float spawnTimer = 0.0f;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        rng.seed(42);

        mainCamera = graphics.GetMainCamera();

        // Set up orthographic camera for 2D view
        if (mainCamera) {
            mainCamera->SetOrthographic(800.0f, 600.0f, -100.0f, 100.0f);
            mainCamera->gameObject->SetPosition(glm::vec3(400.0f, 300.0f, 0.0f));

            // Default camera looks at +X (0 angle). Rotate -90 degrees to look at -Z.
            // We need to reset angles first just in case or apply delta.
            // Since we know it's the default camera, we assume angles are 0.
            // But CameraComponent doesn't expose "SetAngle", only Yaw (delta).
            // A clearer way: The default camera looks +X. We want -Z.
            // That's a -90 degree yaw.
            mainCamera->Yaw(-glm::half_pi<float>());
        }

        // Set gravity (positive Y = up in math/GL coords, so we need negative for down)
        physics2D.SetGravity(glm::vec2(0.0f, -500.0f));

        // Create ground (static body)
        CreateGround();

        // Create walls
        CreateWalls();

        // Set up collision callbacks
        physics2D.SetBeginContactCallback([](Rigidbody2DComponent* a, Rigidbody2DComponent* b) {
            // Flash color on collision
            if (a->gameObject) {
                auto* shape = a->gameObject->GetComponent<ShapeRendererComponent>();
                if (shape) {
                    shape->SetColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                }
            }
        });

        auto* textObj = CreateGameObject();
        textObj->SetName("Physics2D Demo");
        // Screen coordinates: 20, 100
        textObj->SetPosition(glm::vec3(20.0f, 100.0f, 0.0f));

        textObj->AddComponent<TextComponent>(TextProps{
          .text = "Physics2D Demo - Press SPACE to spawn shapes, R to reset",
          //   .fontPath = "assets/fonts/NotoSans-SemiBold.ttf",
          .fontSize = 64.0f,
          .color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
          .layer = CanvasLayer::LAYER_WORLD_2D,
        });
    }

    void CreateGround() {
        ground = CreateGameObject(glm::vec2(400.0f, 50.0f));

        // Add shape renderer for visualization
        ShapeRendererProps shapeProps;
        shapeProps.type = ShapeType2D::Box;
        shapeProps.boxHalfSize = glm::vec2(350.0f, 15.0f);// Half size
        shapeProps.color = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
        shapeProps.filled = true;
        ground->AddComponent<ShapeRendererComponent>(shapeProps);

        // Add static rigidbody
        Rigidbody2DProps rbProps;
        rbProps.type = BodyType2D::Static;
        rbProps.shape.type = ShapeType2D::Box;
        rbProps.shape.boxSize = glm::vec2(700.0f, 30.0f);
        ground->AddComponent<Rigidbody2DComponent>(rbProps);
    }

    void CreateWalls() {
        // Left wall
        auto leftWall = CreateGameObject(glm::vec2(30.0f, 300.0f));
        ShapeRendererProps leftShapeProps;
        leftShapeProps.type = ShapeType2D::Box;
        leftShapeProps.boxHalfSize = glm::vec2(10.0f, 250.0f);
        leftShapeProps.color = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
        leftShapeProps.filled = true;
        leftWall->AddComponent<ShapeRendererComponent>(leftShapeProps);

        Rigidbody2DProps leftRbProps;
        leftRbProps.type = BodyType2D::Static;
        leftRbProps.shape.type = ShapeType2D::Box;
        leftRbProps.shape.boxSize = glm::vec2(20.0f, 500.0f);
        leftWall->AddComponent<Rigidbody2DComponent>(leftRbProps);
        walls.push_back(leftWall);

        // Right wall
        auto rightWall = CreateGameObject(glm::vec2(770.0f, 300.0f));
        ShapeRendererProps rightShapeProps;
        rightShapeProps.type = ShapeType2D::Box;
        rightShapeProps.boxHalfSize = glm::vec2(10.0f, 250.0f);
        rightShapeProps.color = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
        rightShapeProps.filled = true;
        rightWall->AddComponent<ShapeRendererComponent>(rightShapeProps);

        Rigidbody2DProps rightRbProps;
        rightRbProps.type = BodyType2D::Static;
        rightRbProps.shape.type = ShapeType2D::Box;
        rightRbProps.shape.boxSize = glm::vec2(20.0f, 500.0f);
        rightWall->AddComponent<Rigidbody2DComponent>(rightRbProps);
        walls.push_back(rightWall);
    }

    void SpawnRandomShape(glm::vec2 position) {
        std::uniform_int_distribution<int> shapeDist(0, 2);
        std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
        std::uniform_real_distribution<float> sizeDist(20.0f, 50.0f);

        int shapeType = shapeDist(rng);
        glm::vec4 color(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);

        auto body = CreateGameObject(position);

        Rigidbody2DProps rbProps;
        rbProps.type = BodyType2D::Dynamic;
        rbProps.shape.density = 1.0f;
        rbProps.shape.friction = 0.3f;
        rbProps.shape.restitution = 0.4f;

        ShapeRendererProps shapeProps;
        shapeProps.color = color;
        shapeProps.filled = true;// Fill random shapes

        switch (shapeType) {
        case 0: {
            // Box
            float size = sizeDist(rng);
            shapeProps.type = ShapeType2D::Box;
            shapeProps.boxHalfSize = glm::vec2(size * 0.5f, size * 0.5f);

            rbProps.shape.type = ShapeType2D::Box;
            rbProps.shape.boxSize = glm::vec2(size, size);
            break;
        }
        case 1: {
            // Circle
            float radius = sizeDist(rng) * 0.5f;
            shapeProps.type = ShapeType2D::Circle;
            shapeProps.radius = radius;

            rbProps.shape.type = ShapeType2D::Circle;
            rbProps.shape.circleRadius = radius;
            break;
        }
        case 2: {
            // Polygon
            std::uniform_int_distribution<int> vertDist(3, 5);
            int numVerts = vertDist(rng);
            float size = sizeDist(rng);

            std::vector<glm::vec2> vertices;
            for (int i = 0; i < numVerts; ++i) {
                float angle = (2.0f * glm::pi<float>() * i) / numVerts - glm::pi<float>() / 2.0f;
                vertices.push_back(glm::vec2(std::cos(angle) * size * 0.5f, std::sin(angle) * size * 0.5f));
            }
            // For now, assume polygon is convex and centered
            shapeProps.type = ShapeType2D::Polygon;
            shapeProps.vertices = vertices;

            rbProps.shape.type = ShapeType2D::Polygon;
            rbProps.shape.polygonVertices = vertices;
            break;
        }
        }

        body->AddComponent<ShapeRendererComponent>(shapeProps);
        body->AddComponent<Rigidbody2DComponent>(rbProps);
        dynamicBodies.push_back(body);
    }

    void OnUpdate(float dt, float time) override {
        // Spawn shapes on space press
        if (input.IsKeyPressed(Key::SPACE)) {
            std::uniform_real_distribution<float> xDist(100.0f, 700.0f);
            SpawnRandomShape(glm::vec2(xDist(rng), 550.0f));
        }

        // Auto-spawn every second
        spawnTimer += dt;
        if (spawnTimer > 1.0f && dynamicBodies.size() < 50) {
            std::uniform_real_distribution<float> xDist(100.0f, 700.0f);
            SpawnRandomShape(glm::vec2(xDist(rng), 550.0f));
            spawnTimer = 0.0f;
        }

        // Reset colors back to original (fade effect)
        for (auto* body : dynamicBodies) {
            auto* shape = body->GetComponent<ShapeRendererComponent>();
            if (shape) {
                glm::vec4 color = shape->GetColor();
                // Fade white back to original color
                color.r = glm::mix(color.r, 0.7f, dt * 2.0f);
                color.g = glm::mix(color.g, 0.5f, dt * 2.0f);
                color.b = glm::mix(color.b, 0.8f, dt * 2.0f);
                shape->SetColor(color);
            }
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
