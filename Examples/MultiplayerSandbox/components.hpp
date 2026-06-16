#pragma once
#include "Atmospheric.hpp"
#include "game_sim.hpp"
#include "net_lockstep.hpp"
#include <chrono>
#include <string>
#include <fmt/format.h>

// ─── PlayerInputComponent ────────────────────────────────────────────────────
// Samples local keyboard/mouse state and builds an InputFrame each fixed tick.
// Owns curSpell so the HUD can read it without touching LockstepNet.
// Does NOT submit the frame — that is LockstepNetComponent's job.

class PlayerInputComponent : public Component {
public:
    PlayerInputComponent(GameObject* go, GameSim* sim, LockstepNet* net)
        : _sim(sim), _net(net) { gameObject = go; }

    std::string GetName() const override { return "PlayerInput"; }

    InputFrame BuildFrame() {
        _last = Sample();
        return _last;
    }

    uint8_t GetCurSpell() const { return _curSpell; }

    void DrawImGui() override {
        ImGui::Text("Spell slot: %d", int(_curSpell));
        ImGui::Text("Buttons:    0x%02X", _last.buttons);
        ImGui::Text("AimQ:       %d", int(_last.aimQ));
    }

private:
    InputFrame Sample() {
        auto* app = gameObject->GetApp();
        auto* inp = app->GetInput();
        InputFrame f;
        if (inp->IsKeyDown(Key::A) || inp->IsKeyDown(Key::LEFT))  f.buttons |= BTN_LEFT;
        if (inp->IsKeyDown(Key::D) || inp->IsKeyDown(Key::RIGHT)) f.buttons |= BTN_RIGHT;
        if (inp->IsKeyDown(Key::W) || inp->IsKeyDown(Key::SPACE) || inp->IsKeyDown(Key::UP))
            f.buttons |= BTN_JUMP;
        if (inp->IsKeyDown(Key::S) || inp->IsKeyDown(Key::DOWN))  f.buttons |= BTN_DOWN;
        if (app->GetWindow()->GetMouseButtonState())               f.buttons |= BTN_FIRE;

        static const Key spellKeys[] = {
            Key::Num1, Key::Num2, Key::Num3, Key::Num4,
            Key::Num5, Key::Num6, Key::Num7
        };
        for (int i = 0; i < int(SpellType::Count); i++) {
            if (inp->IsKeyDown(spellKeys[i])) _curSpell = uint8_t(i);
        }
        f.spell = _curSpell;

        const Player& me = _sim->players[_net->localPlayer];
        auto fb = app->GetWindow()->GetFramebufferSize();
        glm::vec2 mouse = inp->GetMousePosition();
        float wx = mouse.x * float(SandWorld::W) / float(fb.width);
        float wy = mouse.y * float(SandWorld::H) / float(fb.height);
        f.aimQ = InputFrame::QuantizeAim(std::atan2(wy - me.y, wx - me.x));
        return f;
    }

    GameSim*    _sim;
    LockstepNet* _net;
    uint8_t     _curSpell = 0;
    InputFrame  _last{};
};

// ─── LockstepNetComponent ────────────────────────────────────────────────────
// Owns GameSim and LockstepNet. Drives the fixed-update loop from OnTick so
// the Application class needs no knowledge of ticks, accumulators, or net I/O.
// Also serves as the inspector for sim/net state (replacing GameSimInspectorComponent).

class LockstepNetComponent : public Component {
public:
    explicit LockstepNetComponent(GameObject* go) { gameObject = go; }

    std::string GetName() const override { return "LockstepNet"; }

    void Start(LockstepNet::Mode mode, uint16_t port, uint32_t seed, int delay,
               const std::string& joinIp = "") {
        auto* con = gameObject->GetApp()->GetConsole();
        switch (mode) {
        case LockstepNet::Mode::Host:
            _net.StartHost(port, seed, delay);
            con->Info(fmt::format("Hosting on UDP port {} (seed {})", port, seed));
            break;
        case LockstepNet::Mode::Client:
            _net.StartClient(joinIp, port);
            con->Info(fmt::format("Joining {}:{} ...", joinIp, port));
            break;
        default:
            _net.StartSolo(seed);
            break;
        }
    }

    void Shutdown() { _net.Shutdown(); }

    void SetInputComponent(PlayerInputComponent* ic) { _inputComp = ic; }

    GameSim&     GetSim()       { return _sim; }
    LockstepNet& GetNet()       { return _net; }
    bool         IsStarted() const { return _started; }
    bool         IsStalled() const { return _stalled; }
    bool*        StartedPtr()   { return &_started; }

    void OnTick(float dt) override {
        _net.Pump(NowMs());

        if (!_started && _net.state == LockstepNet::State::Running) {
            _sim.Init(_net.seed);
            _started = true;
            gameObject->GetApp()->GetConsole()->Info(
                fmt::format("Game started, seed {}, local player {}", _net.seed, _net.localPlayer)
            );
        }

        if (_started) RunFixedUpdate(dt);
    }

    void DrawImGui() override {
        if (!_started) { ImGui::TextDisabled("Waiting for connection..."); return; }
        ImGui::Text("Tick: %u", _sim.tick);
        ImGui::Text("Checksum: 0x%08X", _sim.Checksum());
        int aliveProj = 0;
        for (const auto& p : _sim.projectiles) if (p.alive) aliveProj++;
        ImGui::Text("Projectiles alive: %d", aliveProj);
        ImGui::Text("World: %dx%d cells", SandWorld::W, SandWorld::H);
        ImGui::Text("Stalled: %s", _stalled ? "yes" : "no");
        ImGui::Separator();
        static const char* modeNames[]  = { "Solo", "Host", "Client" };
        static const char* stateNames[] = { "Idle", "Connecting", "Running", "Failed" };
        ImGui::Text("Mode: %s  State: %s", modeNames[int(_net.mode)], stateNames[int(_net.state)]);
        if (_net.mode != LockstepNet::Mode::Solo) {
            ImGui::Text("RTT: %d ms  Delay: %d ticks",
                        _net.rttMs < 0 ? 0 : _net.rttMs, _net.inputDelay);
            ImGui::Text("Local player: %d", _net.localPlayer);
            if (_net.desync)
                ImGui::TextColored({ 1, 0, 0, 1 }, "DESYNC DETECTED");
        }
    }

private:
    static uint32_t NowMs() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return uint32_t(duration_cast<milliseconds>(steady_clock::now() - start).count());
    }

    void RunFixedUpdate(float dt) {
        _accum += std::min(dt, 0.25f);
        int guard = 0;
        while (_accum >= TICK_DT) {
            uint32_t t = _sim.tick;
            if (_inputComp)
                _net.SubmitLocalInput(t + uint32_t(_net.inputDelay), _inputComp->BuildFrame());
            if (!_net.HasInputs(t)) { _stalled = true; break; }
            _stalled = false;
            _sim.Step(_net.GetInput(0, t), _net.GetInput(1, t));
            if (t % 120 == 0) _net.ShareChecksum(t, _sim.Checksum());
            _net.PruneBelow(t);
            _accum -= TICK_DT;
            if (++guard >= 5) { _accum = std::min(_accum, TICK_DT); break; }
        }
    }

    GameSim    _sim;
    LockstepNet _net;
    bool        _started = false;
    bool        _stalled = false;
    float       _accum   = 0.0f;
    PlayerInputComponent* _inputComp = nullptr;
};

// ─── PlayerInspectorComponent ────────────────────────────────────────────────
// Inspector-only: holds a raw pointer into one sim player slot and exposes
// state via DrawImGui. No OnTick logic.

class PlayerInspectorComponent : public Component {
public:
    PlayerInspectorComponent(GameObject* go, Player* p, int idx, LockstepNet* net, bool* started)
        : _player(p), _index(idx), _net(net), _started(started) { gameObject = go; }

    std::string GetName() const override { return "Player " + std::to_string(_index); }

    void DrawImGui() override {
        ImGui::PushID(_index);
        if (!*_started) {
            ImGui::TextDisabled("Sim not started");
            ImGui::PopID();
            return;
        }
        ImGui::Text("Alive: %s", _player->alive ? "yes" : "no");
        ImGui::Text("Pos: (%.1f, %.1f)", _player->x, _player->y);
        ImGui::Text("Vel: (%.2f, %.2f)", _player->vx, _player->vy);
        ImGui::Text("Grounded: %s  Levitation: %d", _player->grounded ? "yes" : "no", _player->levitation);
        float hpFrac = float(_player->hp < 0 ? 0 : _player->hp) / float(Player::MAX_HP);
        std::string hpLabel = fmt::format("{}/{}", _player->hp, Player::MAX_HP);
        ImGui::ProgressBar(hpFrac, { -1, 0 }, hpLabel.c_str());
        ImGui::Text("Kills: %d  Deaths: %d", _player->kills, _player->deaths);
        if (_net->mode == LockstepNet::Mode::Solo) {
            ImGui::Separator();
            ImGui::TextDisabled("Solo tweaks (safe only in solo mode)");
            ImGui::SliderInt("HP", &_player->hp, 0, Player::MAX_HP);
            ImGui::Checkbox("Alive", &_player->alive);
        }
        ImGui::PopID();
    }

private:
    Player*      _player;
    int          _index;
    LockstepNet* _net;
    bool*        _started;
};
