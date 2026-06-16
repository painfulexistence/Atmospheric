#pragma once
#include "Atmospheric.hpp"
#include "game_sim.hpp"
#include "net_lockstep.hpp"
#include <string>
#include <fmt/format.h>

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
