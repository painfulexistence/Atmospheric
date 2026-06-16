#pragma once
#include "Atmospheric.hpp"
#include "game_sim.hpp"
#include "net_lockstep.hpp"
#include <string>
#include <fmt/format.h>

// Samples local keyboard/mouse state each fixed tick and submits it to the
// lockstep layer. Owns curSpell so the HUD can query it without touching net.
class PlayerInputComponent : public Component {
public:
    PlayerInputComponent(GameObject* go, GameSim* sim, LockstepNet* net)
        : _sim(sim), _net(net) { gameObject = go; }

    std::string GetName() const override { return "PlayerInput"; }

    void SubmitInput(uint32_t tick) {
        _last = Sample();
        _net->SubmitLocalInput(tick, _last);
    }

    uint8_t GetCurSpell() const { return _curSpell; }

    void DrawImGui() override {
        if (!ImGui::CollapsingHeader("PlayerInput")) return;
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
        auto ws = app->GetWindow()->GetFramebufferSize();
        glm::vec2 mouse = inp->GetMousePosition();
        float wx = mouse.x * float(SandWorld::W) / float(ws.width);
        float wy = mouse.y * float(SandWorld::H) / float(ws.height);
        f.aimQ = InputFrame::QuantizeAim(std::atan2(wy - me.y, wx - me.x));
        return f;
    }

    GameSim*    _sim;
    LockstepNet* _net;
    uint8_t     _curSpell = 0;
    InputFrame  _last{};
};

// Inspector-only components: hold raw pointers into sim state, expose via DrawImGui.
// No OnTick logic — the simulation is driven by FixedUpdate, not the entity loop.

class GameSimInspectorComponent : public Component {
public:
    GameSimInspectorComponent(GameObject* go, GameSim* s, LockstepNet* n, bool* started)
        : _sim(s), _net(n), _started(started) { gameObject = go; }

    std::string GetName() const override { return "GameSim"; }

    void DrawImGui() override {
        if (!ImGui::CollapsingHeader("GameSim")) return;
        if (!*_started) { ImGui::TextDisabled("Waiting for connection..."); return; }
        ImGui::Text("Tick: %u", _sim->tick);
        ImGui::Text("Checksum: 0x%08X", _sim->Checksum());
        int aliveProj = 0;
        for (const auto& p : _sim->projectiles) if (p.alive) aliveProj++;
        ImGui::Text("Projectiles alive: %d", aliveProj);
        ImGui::Text("World: %dx%d cells", SandWorld::W, SandWorld::H);
        ImGui::Separator();
        static const char* modeNames[] = { "Solo", "Host", "Client" };
        static const char* stateNames[] = { "Idle", "Connecting", "Running", "Failed" };
        ImGui::Text("Mode: %s  State: %s", modeNames[int(_net->mode)], stateNames[int(_net->state)]);
        if (_net->mode != LockstepNet::Mode::Solo) {
            ImGui::Text("RTT: %d ms  Delay: %d ticks", _net->rttMs < 0 ? 0 : _net->rttMs, _net->inputDelay);
            ImGui::Text("Local player: %d", _net->localPlayer);
            if (_net->desync)
                ImGui::TextColored({ 1, 0, 0, 1 }, "DESYNC DETECTED");
        }
    }

private:
    GameSim* _sim;
    LockstepNet* _net;
    bool* _started;
};

class PlayerInspectorComponent : public Component {
public:
    PlayerInspectorComponent(GameObject* go, Player* p, int idx, LockstepNet* net, bool* started)
        : _player(p), _index(idx), _net(net), _started(started) { gameObject = go; }

    std::string GetName() const override { return "Player " + std::to_string(_index); }

    void DrawImGui() override {
        std::string header = "Player " + std::to_string(_index);
        if (!ImGui::CollapsingHeader(header.c_str())) return;
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
    Player* _player;
    int _index;
    LockstepNet* _net;
    bool* _started;
};
