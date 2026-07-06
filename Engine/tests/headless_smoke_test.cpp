// Headless smoke test: boots a full Application with AppConfig::headless,
// verifying the server-build path end to end — no window, no GL context, no
// ImGui/RmlUi — while the fixed-tick loop still drives OnUpdate, entity
// ticking, and physics. This is the one Application integration test that can
// run on CI at all: the windowed path needs a display.
// Exit code 0 = pass.
#include <Atmospheric/application.hpp>

#include <cstdio>

namespace {

    constexpr int kTargetUpdates = 60;

    class HeadlessSmokeApp : public Application {
    public:
        HeadlessSmokeApp()
          : Application(
                AppConfig{
                    .fixedTimeStep = 1.0f / 240.0f,// fast ticks keep the test under a second
                    .headless = true,
                }
            ) {
        }

        int updates = 0;
        float lastTime = -1.0f;
        bool timeAdvanced = false;
        GameObject* entity = nullptr;

        void OnInit() override {
            // Procedural world, no scene file — exercises the headless
            // _sceneReady default.
            entity = CreateGameObject(glm::vec3(1.0f, 2.0f, 3.0f));
            entity->SetName("smoke-entity");
        }

        void OnLoad() override {
        }

        void OnUpdate(float /*dt*/, float time) override {
            if (time > lastTime) timeAdvanced = true;
            lastTime = time;
            if (++updates >= kTargetUpdates) Quit();
        }
    };

}// namespace

int main() {
    int updates = 0;
    bool timeAdvanced = false;
    uint64_t clock = 0;
    bool entityAlive = false;

    {
        HeadlessSmokeApp app;
        app.Run();// returns when OnUpdate calls Quit()
        updates = app.updates;
        timeAdvanced = app.timeAdvanced;
        clock = app.GetClock();
        entityAlive = app.entity != nullptr && app.entity->GetName() == "smoke-entity";
    }// full Application teardown must not crash without a window

    if (updates < kTargetUpdates) {
        std::printf("FAIL: only %d/%d updates ran before the loop exited\n", updates, kTargetUpdates);
        return 1;
    }
    if (!timeAdvanced) {
        std::printf("FAIL: GetWindowTime() never advanced in headless mode\n");
        return 1;
    }
    if (clock < uint64_t(kTargetUpdates)) {
        std::printf("FAIL: frame clock %llu < %d\n", static_cast<unsigned long long>(clock), kTargetUpdates);
        return 1;
    }
    if (!entityAlive) {
        std::printf("FAIL: procedural GameObject was not created\n");
        return 1;
    }
    std::printf(
        "HeadlessSmokeTest: %d updates, clock %llu, clean shutdown\n", updates, static_cast<unsigned long long>(clock)
    );
    return 0;
}
