#pragma once
// Explore HUD — an RmlUi document driven through UIPageComponent. Replaces the
// old immediate-mode DrawExploreHUD helper (and the DrawPanel/DrawHPBar/
// DrawMPBar copies it shared with the battle screen). Sync() pushes the current
// player + progression state onto the document each frame.
#include "Atmospheric.hpp"
#include "rpg_entity.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <fmt/format.h>
#include <string>

class HudUIPage : public UIPageComponent {
public:
    using UIPageComponent::UIPageComponent;// inherit (GameObject*, documentPath)

    std::string GetName() const override {
        return "HudUIPage";
    }

    void Sync(const Player& player, const BattleState& battle) {
        if (!GetDocument()) return;// document failed to load — nothing to drive
        const Stats& s = player.stats;
        if (_level) _level->SetInnerRML(fmt::format("LV{} EXP:{}/{}", battle.level, battle.exp, battle.expToNext));
        SetBar(_hpFill, s.hp, s.maxHp);
        SetBar(_mpFill, s.mp, s.maxMp);
        if (_gold) _gold->SetInnerRML(fmt::format("Gold:{}", battle.gold));
    }

protected:
    void OnDocumentLoaded() override {
        _level = GetElement("hud_level");
        _hpFill = GetElement("hud_hp_fill");
        _mpFill = GetElement("hud_mp_fill");
        _gold = GetElement("hud_gold");
    }

private:
    static void SetBar(Rml::Element* fill, int value, int maxValue) {
        if (!fill) return;
        float ratio = maxValue > 0 ? static_cast<float>(value) / static_cast<float>(maxValue) : 0.0f;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        fill->SetProperty("width", fmt::format("{}%", static_cast<int>(ratio * 100.0f)));
    }

    Rml::Element* _level = nullptr;
    Rml::Element* _hpFill = nullptr;
    Rml::Element* _mpFill = nullptr;
    Rml::Element* _gold = nullptr;
};
