#pragma once
// Battle UI — an RmlUi document driven through UIPageComponent. Replaces the
// immediate-mode DrawBattleStats / DrawBattleMenu / DrawBattleLog and the
// hand-rolled panels/bars. The combatant sprites + backdrop are Canvas
// components (see ui_kit.hpp); this owns everything textual and reconciles it
// from BattleState each frame. Cursor state stays in C++ (BattleState.menuSel
// etc.) and is reflected into the document by toggling the ".sel" class — so no
// engine-side keyboard-focus plumbing is needed.
#include "Atmospheric.hpp"
#include "rpg_entity.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <fmt/format.h>
#include <string>

class BattleUIPage : public UIPageComponent {
public:
    using UIPageComponent::UIPageComponent;// inherit (GameObject*, documentPath)

    static constexpr int MAX_ENEMIES = 3;
    static constexpr int MAX_SUB = 8;
    static constexpr int MAX_LOG = 4;

    std::string GetName() const override {
        return "BattleUIPage";
    }

    void Sync(const BattleState& b, const Player& player) {
        if (!GetDocument()) return;// document failed to load — nothing to drive
        SyncStats(player, b);
        SyncEnemies(b);
        SyncMenus(b, player);
        SyncLog(b);
        SyncBanner(b);
    }

protected:
    void OnDocumentLoaded() override {
        _lv = GetElement("bt_lv");
        _hpText = GetElement("bt_hp_text");
        _hpFill = GetElement("bt_hp_fill");
        _mpText = GetElement("bt_mp_text");
        _mpFill = GetElement("bt_mp_fill");
        _atkdef = GetElement("bt_atkdef");
        for (int i = 0; i < MAX_ENEMIES; i++) {
            _enemy[i] = GetElement(fmt::format("bt_enemy{}", i));
            _enemyName[i] = GetElement(fmt::format("bt_enemy{}_name", i));
            _enemyHp[i] = GetElement(fmt::format("bt_enemy{}_hp", i));
        }
        _cmd = GetElement("bt_cmd");
        for (int i = 0; i < 4; i++) _cmdItems[i] = GetElement(fmt::format("bt_cmd{}", i));
        _sub = GetElement("bt_sub");
        _subTitle = GetElement("bt_sub_title");
        for (int i = 0; i < MAX_SUB; i++) _subItems[i] = GetElement(fmt::format("bt_sub{}", i));
        for (int i = 0; i < MAX_LOG; i++) _logLines[i] = GetElement(fmt::format("bt_log{}", i));
        _banner = GetElement("bt_banner");
        _bannerText = GetElement("bt_banner_text");
        Hide();// shown by BattleSystemComponent when a battle starts
    }

private:
    static void ShowEl(Rml::Element* el, bool v) {
        if (el) el->SetProperty("display", v ? "block" : "none");
    }
    static void SetBar(Rml::Element* fill, int value, int maxValue) {
        if (!fill) return;
        float ratio = maxValue > 0 ? static_cast<float>(value) / static_cast<float>(maxValue) : 0.0f;
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        fill->SetProperty("width", fmt::format("{}%", static_cast<int>(ratio * 100.0f)));
    }

    void SyncStats(const Player& player, const BattleState& b) {
        const Stats& s = player.stats;
        if (_lv) _lv->SetInnerRML(fmt::format("LV{}", b.level));
        if (_hpText) _hpText->SetInnerRML(fmt::format("HP {}/{}", s.hp, s.maxHp));
        if (_mpText) _mpText->SetInnerRML(fmt::format("MP {}/{}", s.mp, s.maxMp));
        if (_atkdef) _atkdef->SetInnerRML(fmt::format("ATK:{} DEF:{}", s.atk, s.def));
        SetBar(_hpFill, s.hp, s.maxHp);
        SetBar(_mpFill, s.mp, s.maxMp);
    }

    void SyncEnemies(const BattleState& b) {
        int count = static_cast<int>(b.enemyStats.size());
        for (int i = 0; i < MAX_ENEMIES; i++) {
            bool shown = i < count && !b.enemyStats[i].isDead();
            ShowEl(_enemy[i], shown);
            if (!shown) continue;
            const Stats& es = b.enemyStats[i];
            if (_enemyName[i]) _enemyName[i]->SetInnerRML(b.enemyNames[i]);
            SetBar(_enemyHp[i], es.hp, es.maxHp);
        }
    }

    void SyncMenus(const BattleState& b, const Player& player) {
        const bool cmdOpen = (b.phase == BattlePhase::PlayerMenu);
        const bool skillOpen = (b.phase == BattlePhase::PlayerSkillMenu);
        const bool itemOpen = (b.phase == BattlePhase::PlayerItemMenu);

        ShowEl(_cmd, cmdOpen);
        ShowEl(_sub, skillOpen || itemOpen);

        if (cmdOpen) {
            for (int i = 0; i < 4; i++)
                if (_cmdItems[i]) _cmdItems[i]->SetClass("sel", b.menuSel == i);
        }

        if (skillOpen) {
            if (_subTitle) _subTitle->SetInnerRML("─ Skills ─");
            int count = static_cast<int>(player.skills.size());
            for (int i = 0; i < MAX_SUB; i++) {
                bool has = i < count;
                ShowEl(_subItems[i], has);
                if (!has) continue;
                const Skill& sk = player.skills[i];
                _subItems[i]->SetInnerRML(fmt::format("{} ({}MP)", sk.name, sk.mpCost));
                _subItems[i]->SetClass("sel", b.skillSel == i);
                _subItems[i]->SetClass("dim", false);
            }
        } else if (itemOpen) {
            if (_subTitle) _subTitle->SetInnerRML("─ Items ─");
            int count = static_cast<int>(player.items.size());
            for (int i = 0; i < MAX_SUB; i++) {
                bool has = i < count;
                ShowEl(_subItems[i], has);
                if (!has) continue;
                const Item& it = player.items[i];
                _subItems[i]->SetInnerRML(fmt::format("{} x{}", it.name, it.count));
                _subItems[i]->SetClass("sel", b.itemSel == i);
                _subItems[i]->SetClass("dim", it.count <= 0 && b.itemSel != i);
            }
        }
    }

    void SyncLog(const BattleState& b) {
        int shown = std::min(static_cast<int>(b.log.size()), MAX_LOG);
        for (int i = 0; i < MAX_LOG; i++) {
            if (i < shown) {
                const auto& entry = b.log[b.log.size() - shown + i];
                _logLines[i]->SetInnerRML(entry.text);
                _logLines[i]->SetProperty("opacity", fmt::format("{:.2f}", std::clamp(entry.ttl, 0.0f, 1.0f)));
                ShowEl(_logLines[i], true);
            } else {
                ShowEl(_logLines[i], false);
            }
        }
    }

    void SyncBanner(const BattleState& b) {
        bool victory = (b.phase == BattlePhase::Victory);
        bool defeat = (b.phase == BattlePhase::Defeat);
        ShowEl(_banner, victory || defeat);
        if (_banner) {
            _banner->SetClass("victory", victory);
            _banner->SetClass("defeat", defeat);
        }
        if (_bannerText) _bannerText->SetInnerRML(victory ? "VICTORY!" : defeat ? "DEFEATED" : "");
    }

    Rml::Element *_lv = nullptr, *_hpText = nullptr, *_hpFill = nullptr;
    Rml::Element *_mpText = nullptr, *_mpFill = nullptr, *_atkdef = nullptr;
    Rml::Element *_enemy[MAX_ENEMIES] = {}, *_enemyName[MAX_ENEMIES] = {}, *_enemyHp[MAX_ENEMIES] = {};
    Rml::Element *_cmd = nullptr, *_cmdItems[4] = {};
    Rml::Element *_sub = nullptr, *_subTitle = nullptr, *_subItems[MAX_SUB] = {};
    Rml::Element* _logLines[MAX_LOG] = {};
    Rml::Element *_banner = nullptr, *_bannerText = nullptr;
};
