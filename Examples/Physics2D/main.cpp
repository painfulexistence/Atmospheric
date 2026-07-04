#include "Atmospheric.hpp"
#include "components.hpp"
#include <algorithm>
#include <box2d/box2d.h>
#include <queue>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

static constexpr float gw = 800.0f;
static constexpr float gh = 600.0f;
static constexpr float gshootSpeed = 3000.0f;
static constexpr int gminSides = 3;// triangle
static constexpr int gmaxSides = 9;


// ──────────────────────────────────────────────────────────────────────────────
// Helper: build polygon vertices for a regular n-gon
// ──────────────────────────────────────────────────────────────────────────────
static std::vector<glm::vec2> RegularPolygon(int n, float radius) {
    std::vector<glm::vec2> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) {
        float a = (2.0f * glm::pi<float>() * i) / n - glm::pi<float>() / 2.0f;
        v.push_back({ std::cos(a) * radius, std::sin(a) * radius });
    }
    return v;
}

// ──────────────────────────────────────────────────────────────────────────────
// Colour per side count (3→8)
// ──────────────────────────────────────────────────────────────────────────────
static glm::vec4 SideColor(int sides) {
    // Rainbow hue: 3=red(0°) → 10=violet(270°), evenly spaced
    static const glm::vec4 gpalette[] = {
        { 1.00f, 0.20f, 0.20f, 1.0f },// 3 - red        0°
        { 1.00f, 0.55f, 0.10f, 1.0f },// 4 - orange     38°
        { 1.00f, 0.95f, 0.10f, 1.0f },// 5 - yellow     77°
        { 0.45f, 0.95f, 0.15f, 1.0f },// 6 - lime       115°
        { 0.10f, 0.85f, 0.50f, 1.0f },// 7 - teal       154°
        { 0.10f, 0.65f, 1.00f, 1.0f },// 8 - sky blue   192°
        { 0.75f, 0.15f, 1.00f, 1.0f },// 9 - violet     270° (circle)
    };
    int idx = std::clamp(sides - gminSides, 0, static_cast<int>(std::size(gpalette)) - 1);
    return gpalette[idx];
}

// ──────────────────────────────────────────────────────────────────────────────
// Main game class
// ──────────────────────────────────────────────────────────────────────────────
class PolyMerge : public Application {
    using Application::Application;

    std::mt19937 _rng;
    FontHandle _fontID = 0;

    // All live shape game objects (excluding ground/walls)
    std::vector<GameObject*> _shapes;

    // The shape waiting at the top ready to fire
    GameObject* _pendingShape = nullptr;
    int _pendingShapes = 0;// total fired so far

    // Game state
    bool _launched = false;
    bool _gameOver = false;
    bool _firstLanded = false;// disable merge until first shot lands
    int _score = 0;


    // ── Spawn a shape into the physics world ──────────────────────────────
    GameObject* SpawnShape(glm::vec2 pos, int sides, bool dynamic, float radius = 28.0f) {
        auto* go = CreateGameObject(pos);

        bool useCircle = (sides >= 9);

        ShapeRendererProps sp;
        sp.color = SideColor(sides);
        sp.filled = true;
        if (useCircle) {
            sp.type = ShapeType2D::Circle;
            sp.radius = radius;
        } else {
            sp.type = ShapeType2D::Polygon;
            sp.vertices = RegularPolygon(sides, radius);
        }
        go->AddComponent<ShapeRendererComponent>(sp);

        Rigidbody2DProps rb;
        rb.type = dynamic ? BodyType2D::Dynamic : BodyType2D::Static;
        if (useCircle) {
            rb.shape.type = ShapeType2D::Circle;
            rb.shape.circleRadius = radius;
        } else {
            rb.shape.type = ShapeType2D::Polygon;
            rb.shape.polygonVertices = RegularPolygon(sides, radius);
        }
        rb.shape.density = 1.0f;
        rb.shape.friction = 0.4f;
        rb.shape.restitution = 0.1f;
        // pending shape starts with collision disabled; enabled on launch
        go->AddComponent<Rigidbody2DComponent>(rb);

        go->AddComponent<MergeableComponent>(sides, 3);

        go->AddComponent<ColorRestoreComponent>(3.0f, SideColor(sides));

        return go;
    }

    // ── Place the initial hill of 30 shapes ───────────────────────────────
    void SpawnHill() {
        // Pile shapes in concentric rings centred at (400, 130)
        // so they settle into a mound on the ground
        const glm::vec2 centre{ 400.0f, 130.0f };
        std::uniform_real_distribution<float> xJitter(-12.0f, 12.0f);
        std::uniform_real_distribution<float> yJitter(-12.0f, 12.0f);
        // Weighted: lower sides more common; weights 3→9: 5,4,3,2,2,1,1
        std::discrete_distribution<int> sideDist({ 5, 4, 3, 2, 2, 1, 1 });

        // Layer heights from ground upwards (~60 shapes total)
        constexpr int rows[] = { 11, 10, 10, 9, 8, 7, 5 };
        float y = 95.0f;
        for (int row : rows) {
            float spacing = 58.0f;
            float startX = centre.x - (row - 1) * spacing * 0.5f;
            for (int c = 0; c < row; ++c) {
                glm::vec2 p{ startX + c * spacing + xJitter(_rng), y + yJitter(_rng) };
                int sides = gminSides + sideDist(_rng);
                auto* go = SpawnShape(p, sides, true);
                _shapes.push_back(go);
            }
            y += 58.0f;
        }
    }

    // ── Create the next pending shape at the top centre ───────────────────
    void SpawnPending() {
        std::uniform_int_distribution<int> sideDist(gminSides, gmaxSides);
        int sides = sideDist(_rng);

        _pendingShape = SpawnShape({ gw * 0.5f, gh - 40.0f }, sides, false);
        // Disable collision until launched
        auto* rb = _pendingShape->GetComponent<Rigidbody2DComponent>();
        if (rb) SetBodyCollision(rb->GetBodyId(), false);
        _shapes.push_back(_pendingShape);
        _launched = false;
    }

    // ── Enable/disable all shapes on a body via filter ────────────────────
    static void SetBodyCollision(b2BodyId bodyId, bool enabled) {
        int count = b2Body_GetShapeCount(bodyId);
        if (count <= 0) return;
        std::vector<b2ShapeId> shapes(count);
        b2Body_GetShapes(bodyId, shapes.data(), count);
        for (auto shapeId : shapes) {
            b2Filter f = enabled ? b2DefaultFilter() : b2Filter{ 0, 0, 0 };
            b2Shape_SetFilter(shapeId, f);
        }
    }

    // ── Fire the pending shape toward the crosshair ───────────────────────
    void Launch(glm::vec2 mousePos) {
        if (!_pendingShape || _launched) return;

        auto* rb = _pendingShape->GetComponent<Rigidbody2DComponent>();
        if (!rb) return;

        glm::vec2 origin{ gw * 0.5f, gh - 40.0f };
        // Horizontal-only aim: x from cursor direction, y fixed upward arc
        glm::vec2 dir = glm::normalize(mousePos - origin);
        glm::vec2 vel = glm::vec2(dir.x * gshootSpeed, 0.0f);

        // Mark as launched, re-enable collision, switch to dynamic, enable CCD
        _pendingShape->GetComponent<MergeableComponent>()->launched = true;
        SetBodyCollision(rb->GetBodyId(), true);
        rb->SetBodyType(BodyType2D::Dynamic);
        b2Body_SetBullet(rb->GetBodyId(), true);
        rb->SetLinearVelocity(vel);

        _launched = true;
        _pendingShape = nullptr;

        DeferSpawn([this] { SpawnPending(); });
    }

    // ── Flood-fill: collect all connected shapes with the same side count ─
    std::vector<GameObject*> FloodFill(GameObject* start, int sides) {
        std::unordered_set<GameObject*> visited;
        std::queue<GameObject*> q;
        q.push(start);
        visited.insert(start);

        const float contactDist = 85.0f;// pixel distance to count as "touching"

        while (!q.empty()) {
            auto* cur = q.front();
            q.pop();
            auto* rbCur = cur->GetComponent<Rigidbody2DComponent>();
            if (!rbCur) continue;
            glm::vec2 posA = rbCur->GetPosition();

            for (auto* other : _shapes) {
                if (!other->isActive) continue;
                if (visited.count(other)) continue;
                auto* mc = other->GetComponent<MergeableComponent>();
                if (!mc || mc->sides != sides) continue;

                auto* rbOther = other->GetComponent<Rigidbody2DComponent>();
                if (!rbOther) continue;
                glm::vec2 posB = rbOther->GetPosition();
                float dist = glm::length(posB - posA);
                if (dist < contactDist) {
                    visited.insert(other);
                    q.push(other);
                }
            }
        }

        return { visited.begin(), visited.end() };
    }

    // ── Handle a merge event ──────────────────────────────────────────────
    void TryMerge(GameObject* goA, GameObject* goB) {
        auto* mcA = goA->GetComponent<MergeableComponent>();
        auto* mcB = goB->GetComponent<MergeableComponent>();
        if (!mcA || !mcB) return;
        if (mcA->sides != mcB->sides) return;
        if (mcA->pendingRemove || mcB->pendingRemove) return;

        int sides = mcA->sides;
        // Collect the whole connected group
        auto group = FloodFill(goA, sides);
        if (static_cast<int>(group.size()) < mcA->minGroup) return;

        // Mark all as pending remove so re-entrant callbacks skip them
        for (auto* go : group)
            go->GetComponent<MergeableComponent>()->pendingRemove = true;

        int n = static_cast<int>(group.size());

        // Calculate centroid
        glm::vec2 centroid{ 0, 0 };
        for (auto* go : group)
            centroid += go->GetComponent<Rigidbody2DComponent>()->GetPosition();
        centroid /= static_cast<float>(n);

        int newSides = sides + 1;
        if (sides >= gmaxSides) {
            _score *= 2;
            newSides = -1;
        } else {
            _score += (1 << sides) * 10 * (n - 1);
        }

        // Disable all merged shapes
        DeferSpawn([this, group, centroid, newSides]() {
            for (auto* go : group) {
                auto* rb = go->GetComponent<Rigidbody2DComponent>();
                if (rb && b2Body_IsValid(rb->GetBodyId())) b2Body_Disable(rb->GetBodyId());
                go->SetActive(false);
            }
            if (newSides > 0) {
                auto* newGo = SpawnShape(centroid, newSides, true);
                _shapes.push_back(newGo);
            }
        });
    }

    // ── Draw dashed line ──────────────────────────────────────────────────
    void DrawDashedLine(glm::vec2 a, glm::vec2 b, float dashLen, float gapLen, glm::vec4 color) {
        glm::vec2 dir = b - a;
        float total = glm::length(dir);
        if (total < 0.001f) return;
        dir /= total;
        float t = 0.0f;
        bool drawing = true;
        while (t < total) {
            float segLen = drawing ? dashLen : gapLen;
            float t2 = std::min(t + segLen, total);
            if (drawing) {
                glm::vec2 p1 = a + dir * t;
                glm::vec2 p2 = a + dir * t2;
                GraphicsSubsystem::Get()->DrawLine(p1.x, p1.y, p2.x, p2.y, color);
            }
            t = t2;
            drawing = !drawing;
        }
    }

    void OnInit() override {
        ComponentFactory::Register("MergeableComponent", [](GameObject* o, Deserializer& d) -> Component* {
            int sides = 3, minGroup = 2;
            d.Read("sides", sides);
            d.Read("minGroup", minGroup);
            return new MergeableComponent(o, sides, minGroup);
        });
        ComponentFactory::Register("ColorRestoreComponent", [](GameObject* o, Deserializer& d) -> Component* {
            float speed = 2.0f;
            glm::vec4 target(-1.0f);
            d.Read("speed", speed);
            d.Read("target", target);
            return new ColorRestoreComponent(o, speed, target);
        });
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {

        _rng.seed(std::random_device{}());
        _score = 0;
        _gameOver = false;
        _launched = false;
        _firstLanded = false;
        _pendingShape = nullptr;
        _shapes.clear();

        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 32.0f);

        mainCamera = GraphicsSubsystem::Get()->GetMainCamera();

        Physics2DSubsystem::Get()->SetGravity({ 0.0f, -520.0f });

        // Ground
        {
            auto* go = CreateGameObject({ gw * 0.5f, 42.0f });
            ShapeRendererProps sp;
            sp.type = ShapeType2D::Box;
            sp.boxHalfSize = { 360.0f, 18.0f };
            sp.color = { 0.35f, 0.35f, 0.40f, 1.0f };
            sp.filled = true;
            go->AddComponent<ShapeRendererComponent>(sp);
            Rigidbody2DProps rb;
            rb.type = BodyType2D::Static;
            rb.shape.type = ShapeType2D::Box;
            rb.shape.boxSize = { 720.0f, 36.0f };
            go->AddComponent<Rigidbody2DComponent>(rb);
        }
        // Left wall
        {
            auto* go = CreateGameObject({ 38.0f, gh * 0.5f });
            ShapeRendererProps sp;
            sp.type = ShapeType2D::Box;
            sp.boxHalfSize = { 12.0f, gh * 0.5f };
            sp.color = { 0.35f, 0.35f, 0.40f, 1.0f };
            sp.filled = true;
            go->AddComponent<ShapeRendererComponent>(sp);
            Rigidbody2DProps rb;
            rb.type = BodyType2D::Static;
            rb.shape.type = ShapeType2D::Box;
            rb.shape.boxSize = { 24.0f, gh };
            go->AddComponent<Rigidbody2DComponent>(rb);
        }
        // Right wall
        {
            auto* go = CreateGameObject({ gw - 38.0f, gh * 0.5f });
            ShapeRendererProps sp;
            sp.type = ShapeType2D::Box;
            sp.boxHalfSize = { 12.0f, gh * 0.5f };
            sp.color = { 0.35f, 0.35f, 0.40f, 1.0f };
            sp.filled = true;
            go->AddComponent<ShapeRendererComponent>(sp);
            Rigidbody2DProps rb;
            rb.type = BodyType2D::Static;
            rb.shape.type = ShapeType2D::Box;
            rb.shape.boxSize = { 24.0f, gh };
            go->AddComponent<Rigidbody2DComponent>(rb);
        }

        // Collision callback: first launched-shape contact unlocks merge globally
        Physics2DSubsystem::Get()->SetBeginContactCallback([this](Rigidbody2DComponent* a, Rigidbody2DComponent* b) {
            if (!a || !b || !a->gameObject || !b->gameObject) return;
            if (a->GetBodyType() != BodyType2D::Dynamic) return;
            if (b->GetBodyType() != BodyType2D::Dynamic) return;
            auto* ma = a->gameObject->GetComponent<MergeableComponent>();
            auto* mb = b->gameObject->GetComponent<MergeableComponent>();
            if (!ma || !mb) return;
            if (!_firstLanded && (ma->launched || mb->launched)) _firstLanded = true;
            if (!_firstLanded) return;
            if (ma->pendingRemove || mb->pendingRemove) return;
            if (ma->sides == mb->sides) TryMerge(a->gameObject, b->gameObject);
        });

        SpawnHill();
        SpawnPending();
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) {
            Quit();
            return;
        }

        auto dpi = Window::Get()->GetDPI();
        glm::vec2 mouse = InputSubsystem::Get()->GetMousePosition() / dpi;
        mouse.y = gh - mouse.y;

        // ── Firing ────────────────────────────────────────────────────────
        if (!_gameOver && !_launched && _pendingShape) {
            if (InputSubsystem::Get()->IsKeyPressed(Key::SPACE) || InputSubsystem::Get()->IsMouseButtonPressed()) {
                Launch(mouse);
            }
        }

        // ── Draw dashed aim line ──────────────────────────────────────────
        if (!_launched && _pendingShape) {
            glm::vec2 origin{ gw * 0.5f, gh - 40.0f };
            glm::vec4 lineColor{ 1.0f, 1.0f, 1.0f, 0.65f };
            DrawDashedLine(origin, mouse, 10.0f, 6.0f, lineColor);
        }

        // ── Crosshair ─────────────────────────────────────────────────────
        float mx = mouse.x, my = mouse.y;
        GraphicsSubsystem::Get()->DrawLine(mx - 10, my, mx + 10, my, { 1, 1, 1, 0.9f });
        GraphicsSubsystem::Get()->DrawLine(mx, my - 10, mx, my + 10, { 1, 1, 1, 0.9f });
        GraphicsSubsystem::Get()->DrawCircle(mx, my, 5.0f, { 1, 1, 1, 0.4f });

        // ── HUD ───────────────────────────────────────────────────────────
        if (_fontID) {
            std::string scoreTxt = "Score: " + std::to_string(_score);
            GraphicsSubsystem::Get()->DrawText(_fontID, scoreTxt, 12.0f, gh - 36.0f, 1.0f, { 1, 1, 1, 1 });
            GraphicsSubsystem::Get()->DrawText(
                _fontID, "SPACE / LMB = fire", 12.0f, gh - 64.0f, 0.7f, { 0.8f, 0.8f, 0.8f, 0.8f }
            );

            // Side legend (uncomment to show)
            // for (int s = MIN_SIDES; s <= MAX_SIDES; ++s) {
            //     float y = H - 36.0f - (s - MIN_SIDES) * 22.0f;
            //     GraphicsSubsystem::Get()->DrawText(fontID, std::to_string(s) + "-sided", W - 100.0f, y, 0.65f,
            //     SideColor(s));
            // }
        }
    }
};

int main(int /*argc*/, char* /*argv*/[]) {
    PolyMerge game(
        { .windowTitle = "PolyMerge - Shape Merge Puzzle",
          .windowWidth = static_cast<int>(gw),
          .windowHeight = static_cast<int>(gh),
          .useDefaultTextures = true,
          .useDefaultShaders = true,
          .preset = "2D" }
    );
    game.Run();
    return 0;
}
