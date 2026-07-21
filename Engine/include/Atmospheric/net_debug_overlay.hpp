#pragma once
#include "component.hpp"
#include "net_debug_controls.hpp"// DialConditioner + ConditionerKeys (pulls input_subsystem + net_conditioner)
#include "net_hud.hpp"// DrawNetHud (pulls graphics_subsystem + net_metrics + net_conditioner)
#include "window.hpp"

#include <string>

// NetDebugOverlay — the netgraph as an attachable Component, the engine-idiomatic
// way to add it. Instead of calling DialConditioner + DrawNetHud by hand in your
// update loop, point this at your endpoint's metrics + conditioner and attach it:
//
//   auto* cam = GraphicsSubsystem::Get()->GetMainCamera();
//   cam->gameObject->AddComponent<NetDebugOverlay>(&net.Metrics(), &net.Conditioner(), fontId);
//   // MultiplayerSandbox: the number row is spells, so pass a letter keymap:
//   //   ConditionerKeys k; k.latencyDown = Key::J; ...;
//   //   ...AddComponent<NetDebugOverlay>(&net.Metrics(), &net.Conditioner(), fontId, k);
//
// Each tick it dials the emulated link from the keybinds (set dialKeys = false to
// disable) and draws the netgraph top-right. The pointed-at NetMetrics /
// NetConditioner must outlive the component (they live on the net object, which
// outlives its GameObjects — fine in every current example).
class NetDebugOverlay : public Component {
public:
    NetDebugOverlay(
        GameObject* owner,
        const NetMetrics* metrics,
        NetConditioner* conditioner,
        FontHandle font,
        ConditionerKeys keys = {}
    )
      : _metrics(metrics), _cond(conditioner), _font(font), _keys(keys) {
        gameObject = owner;
    }

    std::string GetName() const override {
        return "NetDebugOverlay";
    }

    void OnTick(float /*dt*/) override {
        if (!_metrics || !_cond) return;
        if (dialKeys) {
            if (auto* input = InputSubsystem::Get()) DialConditioner(input, *_cond, _keys);
        }
        auto* gfx = GraphicsSubsystem::Get();
        auto* win = Window::Get();
        if (gfx && win) {
            const auto ws = win->GetLogicalSize();
            DrawNetHud(gfx, _font, *_metrics, *_cond, static_cast<float>(ws.width) - 258.0f, 20.0f);
        }
    }

    bool dialKeys = true;// let the keybinds (see ConditionerKeys) dial the emulated link

private:
    const NetMetrics* _metrics;
    NetConditioner* _cond;
    FontHandle _font;
    ConditionerKeys _keys;
};
