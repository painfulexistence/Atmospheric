// ============================================================================
//  CardBattle — a Slay-the-Spire / Darkest-Dungeon-flavoured card battler
//  built on the Atmospheric engine.
//
//  Design goals (as requested):
//    * Numbers are easy to tweak — ALL content & tuning lives in game_data.hpp.
//    * Component-based architecture — combatants are GameObjects assembled from
//      HealthComponent / StatusComponent / EnergyComponent / DeckComponent /
//      EnemyBrainComponent / CombatantComponent, and the whole fight is driven
//      by a BattleManagerComponent. See components/ and battle_manager.hpp.
//
//  This file is the thin presentation layer: it lays out the screen, renders
//  everything with the engine's immediate-mode 2D API, and translates mouse /
//  keyboard into intents on the BattleManagerComponent. No game rules here.
// ============================================================================
#include "Atmospheric.hpp"
#include "battle_manager.hpp"

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace CardGame;
using glm::vec2;
using glm::vec4;

class CardBattleGame : public Application {
    using Application::Application;

    // ---- resources & state -------------------------------------------------
    FontHandle _font = 0;
    BattleManagerComponent* _mgr = nullptr;

    const float W = 1280.0f;
    const float H = 720.0f;

    // per-frame layout caches (shared by input + render)
    std::vector<Rect> _handRects;
    int   _hoverCard = -1;
    Rect  _endTurnBtn;
    std::vector<Rect> _rewardRects;

    bool  _prevMouseDown = false;

    // ---- palette -----------------------------------------------------------
    const vec4 COL_BG      {0.10f, 0.09f, 0.13f, 1.0f};
    const vec4 COL_PANEL   {0.18f, 0.17f, 0.24f, 1.0f};
    const vec4 COL_BORDER  {0.40f, 0.40f, 0.55f, 0.9f};
    const vec4 COL_ENEMY   {0.32f, 0.17f, 0.19f, 1.0f};
    const vec4 COL_PLAYER  {0.16f, 0.23f, 0.31f, 1.0f};
    const vec4 COL_HP_BG   {0.08f, 0.08f, 0.08f, 0.95f};
    const vec4 COL_HP      {0.30f, 0.80f, 0.32f, 1.0f};
    const vec4 COL_BLOCK   {0.55f, 0.78f, 0.95f, 1.0f};
    const vec4 COL_SELECT  {1.00f, 0.85f, 0.30f, 1.0f};
    const vec4 COL_WHITE   {0.95f, 0.95f, 0.98f, 1.0f};
    const vec4 COL_DIM     {0.55f, 0.55f, 0.62f, 1.0f};

    // ========================================================================
    //  lifecycle
    // ========================================================================
    void OnInit() override {
        ComponentFactory::Register("HealthComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              int maxHp = 100;
              d.Read("maxHp", maxHp);
              return new HealthComponent(o, maxHp);
          });
        ComponentFactory::Register("EnergyComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              return new EnergyComponent(o);
          });
        ComponentFactory::Register("StatusComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              return new StatusComponent(o);
          });
        ComponentFactory::Register("CombatantComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              std::string name = "Combatant";
              bool isPlayer = false;
              d.Read("name", name);
              d.Read("isPlayer", isPlayer);
              return new CombatantComponent(o, name, isPlayer);
          });
        ComponentFactory::Register("DeckComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              std::vector<int> startingDeck;
              int seedVal = 0;
              d.Read("startingDeck", startingDeck);
              d.Read("seed", seedVal);
              return new DeckComponent(o, startingDeck, static_cast<unsigned int>(seedVal));
          });
        ComponentFactory::Register("EnemyBrainComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              int enemyId = 0;
              int seedVal = 0;
              d.Read("enemyId", enemyId);
              d.Read("seed", seedVal);
              const auto& db = EnemyDB();
              if (enemyId < 0 || enemyId >= static_cast<int>(db.size())) {
                  enemyId = 0;
              }
              return new EnemyBrainComponent(o, &db[enemyId], static_cast<unsigned int>(seedVal));
          });
        ComponentFactory::Register("BattleManagerComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              return new BattleManagerComponent(o, o->GetApp());
          });
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _font = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        for (const auto& go : GetEntities()) {
            if (go->GetName() == "BattleManager") {
                _mgr = go->GetComponent<BattleManagerComponent>();
                break;
            }
        }
        if (_mgr) _mgr->StartRun();
    }

    void OnUpdate(float dt, float /*time*/) override {
        layoutFrame();
        handleInput();
        render();
        if (input.IsKeyPressed(Key::ESCAPE)) Quit();
    }

    // ========================================================================
    //  layout (runs before input & render so both agree on rectangles)
    // ========================================================================
    void layoutFrame() {
        // Enemies along the top, centered.
        const auto& enemies = _mgr->enemies();
        const float EW = 140.0f, EH = 150.0f, gap = 48.0f;
        int n = static_cast<int>(enemies.size());
        float total = n * EW + (n - 1) * gap;
        float startX = (W - total) * 0.5f;
        for (int i = 0; i < n; ++i)
            enemies[i]->rect = { startX + i * (EW + gap), 90.0f, EW, EH };

        // Player on the left.
        if (_mgr->player())
            _mgr->player()->rect = { 70.0f, 330.0f, 170.0f, 190.0f };

        // Hand along the bottom, centered.
        _handRects.clear();
        const auto& hand = _mgr->deck()->Hand();
        const float CW = 116.0f, CH = 166.0f, cgap = 12.0f;
        int hn = static_cast<int>(hand.size());
        float htotal = hn * CW + std::max(0, hn - 1) * cgap;
        float hstart = (W - htotal) * 0.5f;
        float baseY = H - CH - 24.0f;
        for (int i = 0; i < hn; ++i)
            _handRects.push_back({ hstart + i * (CW + cgap), baseY, CW, CH });

        // hovered card
        vec2 m = input.GetMousePosition() / Window::Get()->GetDPI();
        _hoverCard = -1;
        for (int i = hn - 1; i >= 0; --i)
            if (_handRects[i].Contains(m)) { _hoverCard = i; break; }

        // End-turn button
        _endTurnBtn = { W - 200.0f, H - 90.0f, 160.0f, 50.0f };

        // Reward choices (centered row)
        _rewardRects.clear();
        if (_mgr->phase() == Phase::Reward) {
            const auto& rc = _mgr->rewardCards();
            int rn = static_cast<int>(rc.size());
            float rtotal = rn * CW + std::max(0, rn - 1) * 40.0f;
            float rstart = (W - rtotal) * 0.5f;
            for (int i = 0; i < rn; ++i)
                _rewardRects.push_back({ rstart + i * (CW + 40.0f), H * 0.5f - CH * 0.5f, CW, CH });
        }
    }

    // ========================================================================
    //  input  ->  intents on the battle manager
    // ========================================================================
    void handleInput() {
        vec2 m = input.GetMousePosition() / Window::Get()->GetDPI();
        bool down  = Window::Get()->GetMouseButtonState();
        bool click = down && !_prevMouseDown;
        _prevMouseDown = down;

        switch (_mgr->phase()) {
        case Phase::PlayerTurn: {
            // end turn
            if (input.IsKeyPressed(Key::SPACE) || input.IsKeyPressed(Key::E) ||
                input.IsKeyPressed(Key::ENTER) ||
                (click && _endTurnBtn.Contains(m))) {
                _mgr->EndTurn();
                break;
            }
            // cycle target
            if (input.IsKeyPressed(Key::T)) _mgr->CycleTarget();

            // select target by clicking an enemy
            if (click) {
                const auto& enemies = _mgr->enemies();
                for (int i = 0; i < static_cast<int>(enemies.size()); ++i)
                    if (enemies[i]->IsAlive() && enemies[i]->rect.Contains(m))
                        _mgr->SetTarget(i);
            }

            // play a card by clicking it
            if (click && _hoverCard >= 0)
                _mgr->TryPlayCard(_hoverCard, _mgr->selectedTarget());

            // play a card with number keys 1..9
            for (int i = 0; i < 9; ++i)
                if (input.IsKeyPressed(numKey(i)))
                    _mgr->TryPlayCard(i, _mgr->selectedTarget());
            break;
        }
        case Phase::Reward: {
            for (int i = 0; i < static_cast<int>(_rewardRects.size()); ++i) {
                if (input.IsKeyPressed(numKey(i)) ||
                    (click && _rewardRects[i].Contains(m))) {
                    _mgr->TakeReward(i);
                    return;
                }
            }
            if (input.IsKeyPressed(Key::SPACE) || input.IsKeyPressed(Key::Num0))
                _mgr->TakeReward(-1); // skip
            break;
        }
        case Phase::Won:
        case Phase::Defeat:
            if (input.IsKeyPressed(Key::R)) _mgr->StartRun();
            break;
        case Phase::EnemyTurn:
        default:
            break;
        }
    }

    static Key numKey(int i) {
        static const Key keys[9] = { Key::Num1, Key::Num2, Key::Num3, Key::Num4,
                                     Key::Num5, Key::Num6, Key::Num7, Key::Num8,
                                     Key::Num9 };
        return (i >= 0 && i < 9) ? keys[i] : Key::UNKNOWN;
    }

    // ========================================================================
    //  rendering
    // ========================================================================
    void render() {
        auto* g = GraphicsSubsystem::Get();
        g->DrawQuad(W * 0.5f, H * 0.5f, W, H, 0, COL_BG);

        drawTopBar();
        for (auto* e : _mgr->enemies()) drawEnemy(e);
        if (_mgr->player()) drawPlayer(_mgr->player());
        drawHand();
        drawEnergyAndButtons();
        drawLog();

        switch (_mgr->phase()) {
        case Phase::EnemyTurn:
            if (_mgr->bannerTimer() > 0.0f) drawBanner("ENEMY TURN", COL_SELECT);
            break;
        case Phase::Reward:  drawReward();  break;
        case Phase::Won:     drawEndScreen("VICTORY!",  vec4(0.4f,1,0.5f,1)); break;
        case Phase::Defeat:  drawEndScreen("DEFEATED",  vec4(1,0.4f,0.4f,1)); break;
        default: break;
        }
    }

    void drawTopBar() {
        auto* g = GraphicsSubsystem::Get();
        std::string s = fmt::format("Encounter {} / {}",
                                    _mgr->encounterIndex() + 1, _mgr->encounterCount());
        g->DrawText(_font, s, 20, 18, 0.65f, COL_WHITE);
        g->DrawText(_font, "1-9 play  |  click enemy = target  |  T cycle  |  SPACE/E end turn",
                    20, 44, 0.45f, COL_DIM);
    }

    void drawPanel(Rect r, vec4 bg, vec4 border) {
        auto* g = GraphicsSubsystem::Get();
        g->DrawQuad(r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w, r.h, 0, bg);
        g->DrawRect(r.x, r.y, r.w, r.h, border);
    }

    void drawBar(float x, float y, float w, float h, int cur, int max, vec4 fill) {
        auto* g = GraphicsSubsystem::Get();
        g->DrawQuad(x + w * 0.5f, y + h * 0.5f, w, h, 0, COL_HP_BG);
        float ratio = max > 0 ? std::clamp(static_cast<float>(cur) / max, 0.0f, 1.0f) : 0.0f;
        float fw = (w - 2) * ratio;
        if (fw > 0) g->DrawQuad(x + 1 + fw * 0.5f, y + 1 + (h - 2) * 0.5f, fw, h - 2, 0, fill);
    }

    void drawTextCentered(const std::string& t, float cx, float y, float scale, vec4 col) {
        auto* g = GraphicsSubsystem::Get();
        vec2 sz = g->MeasureText(_font, t, scale);
        g->DrawText(_font, t, cx - sz.x * 0.5f, y, scale, col);
    }

    std::string statusLine(StatusComponent* s) {
        std::string out;
        auto add = [&](StatusType t) {
            if (s->Get(t) > 0) {
                if (!out.empty()) out += " ";
                out += fmt::format("{}{}", StatusName(t), s->Get(t));
            }
        };
        add(StatusType::Strength);
        add(StatusType::Vulnerable);
        add(StatusType::Weak);
        add(StatusType::Poison);
        return out;
    }

    void drawCombatantCommon(CombatantComponent* c, Rect r, vec4 boxColor) {
        auto* g = GraphicsSubsystem::Get();
        drawPanel(r, boxColor, COL_BORDER);

        // damage flash overlay
        if (c->hitFlash > 0.0f)
            g->DrawQuad(r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w, r.h, 0,
                        vec4(1.0f, 0.3f, 0.3f, 0.45f * c->hitFlash));

        // name
        drawTextCentered(c->DisplayName(), r.Center().x, r.y - 26, 0.55f, COL_WHITE);

        // HP bar + text under the box
        float by = r.y + r.h + 6;
        auto* hp = c->Health();
        drawBar(r.x, by, r.w, 16, hp->Hp(), hp->MaxHp(), COL_HP);
        drawTextCentered(fmt::format("{}/{}", hp->Hp(), hp->MaxHp()),
                         r.Center().x, by + 1, 0.42f, COL_WHITE);

        // block badge (top-left of box)
        if (hp->Block() > 0) {
            g->DrawCircle(r.x + 16, r.y + 16, 15, COL_BLOCK);
            drawTextCentered(std::to_string(hp->Block()), r.x + 16, r.y + 6, 0.45f,
                             vec4(0.05f, 0.1f, 0.15f, 1));
        }

        // status line below HP
        std::string st = statusLine(c->Status());
        if (!st.empty())
            drawTextCentered(st, r.Center().x, by + 22, 0.42f, vec4(1, 0.8f, 0.5f, 1));
    }

    void drawEnemy(CombatantComponent* e) {
        Rect r = e->rect;
        bool dead = !e->IsAlive();
        drawCombatantCommon(e, r, dead ? vec4(0.15f,0.13f,0.14f,1) : COL_ENEMY);

        if (dead) {
            drawTextCentered("DEAD", r.Center().x, r.Center().y - 8, 0.6f, COL_DIM);
            return;
        }

        // selection highlight
        const auto& enemies = _mgr->enemies();
        int sel = _mgr->selectedTarget();
        bool selected = (sel >= 0 && sel < static_cast<int>(enemies.size()) && enemies[sel] == e);
        if (selected) {
            auto* g = GraphicsSubsystem::Get();
            g->DrawRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, COL_SELECT);
            g->DrawRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, COL_SELECT);
        }

        // intent telegraph above the box
        IntentKind intent = _mgr->telegraphIntent(e);
        int dmg = _mgr->telegraphDamage(e);
        std::string itext;
        vec4 icol;
        switch (intent) {
            case IntentKind::Attack: itext = fmt::format("ATK {}", dmg); icol = vec4(1,0.45f,0.4f,1); break;
            case IntentKind::Defend: itext = "DEF";   icol = COL_BLOCK; break;
            case IntentKind::Buff:   itext = "BUFF";  icol = vec4(1,0.85f,0.4f,1); break;
            case IntentKind::Debuff: itext = "DEBUFF";icol = vec4(0.8f,0.5f,1,1); break;
        }
        drawTextCentered(itext, r.Center().x, r.y - 52, 0.55f, icol);
    }

    void drawPlayer(CombatantComponent* p) {
        drawCombatantCommon(p, p->rect, COL_PLAYER);
    }

    void drawHand() {
        auto* g = GraphicsSubsystem::Get();
        const auto& hand = _mgr->deck()->Hand();
        int energy = _mgr->energy()->Energy();

        if (_handRects.size() < hand.size()) {
            _handRects.clear();
            const float CW = 116.0f, CH = 166.0f, cgap = 12.0f;
            int hn = static_cast<int>(hand.size());
            float htotal = hn * CW + std::max(0, hn - 1) * cgap;
            float hstart = (W - htotal) * 0.5f;
            float baseY = H - CH - 24.0f;
            for (int i = 0; i < hn; ++i)
                _handRects.push_back({ hstart + i * (CW + cgap), baseY, CW, CH });
        }

        for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
            const CardDef& card = GetCard(hand[i]);
            Rect r = _handRects[i];
            bool hovered = (i == _hoverCard);
            if (hovered) r.y -= 22;
            bool affordable = (energy >= card.cost) && _mgr->phase() == Phase::PlayerTurn;

            vec4 base;
            switch (card.kind) {
                case CardKind::Attack: base = vec4(0.45f,0.22f,0.22f,1); break;
                case CardKind::Skill:  base = vec4(0.22f,0.30f,0.45f,1); break;
                case CardKind::Power:  base = vec4(0.45f,0.38f,0.18f,1); break;
            }
            if (!affordable) base = vec4(base.r*0.5f, base.g*0.5f, base.b*0.5f, 1);

            drawPanel(r, base, hovered ? COL_SELECT : COL_BORDER);

            // cost orb
            g->DrawCircle(r.x + 16, r.y + 16, 14, vec4(0.15f,0.15f,0.25f,1));
            drawTextCentered(std::to_string(card.cost), r.x + 16, r.y + 6, 0.5f,
                             affordable ? COL_WHITE : COL_DIM);

            // name
            drawTextCentered(card.name, r.Center().x, r.y + 34, 0.42f, COL_WHITE);

            // rules text, wrapped to a couple of lines
            drawWrapped(card.text, r.x + 8, r.y + 70, r.w - 16, 0.36f, COL_WHITE);

            // key hint
            drawTextCentered(fmt::format("[{}]", i + 1), r.Center().x, r.y + r.h - 22,
                             0.42f, COL_DIM);
        }
    }

    void drawWrapped(const std::string& text, float x, float y, float maxW,
                     float scale, vec4 col) {
        auto* g = GraphicsSubsystem::Get();
        std::istringstream iss(text);
        std::string word, line;
        float lh = 16.0f;
        auto flush = [&](const std::string& l) {
            if (!l.empty()) { g->DrawText(_font, l, x, y, scale, col); y += lh; }
        };
        while (iss >> word) {
            std::string trial = line.empty() ? word : line + " " + word;
            if (g->MeasureText(_font, trial, scale).x > maxW && !line.empty()) {
                flush(line);
                line = word;
            } else {
                line = trial;
            }
        }
        flush(line);
    }

    void drawEnergyAndButtons() {
        auto* g = GraphicsSubsystem::Get();

        // energy orb
        g->DrawCircle(70, H - 90, 34, vec4(0.20f,0.35f,0.55f,1));
        drawTextCentered(fmt::format("{}/{}", _mgr->energy()->Energy(),
                                     _mgr->energy()->MaxEnergy()),
                         70, H - 100, 0.55f, COL_WHITE);
        drawTextCentered("Energy", 70, H - 48, 0.4f, COL_DIM);

        // pile counts
        g->DrawText(_font, fmt::format("Draw: {}", _mgr->deck()->DrawCount()),
                    140, H - 60, 0.45f, COL_DIM);
        g->DrawText(_font, fmt::format("Discard: {}", _mgr->deck()->DiscardCount()),
                    140, H - 38, 0.45f, COL_DIM);

        // end-turn button
        bool active = _mgr->phase() == Phase::PlayerTurn;
        drawPanel(_endTurnBtn, active ? vec4(0.30f,0.45f,0.25f,1) : vec4(0.2f,0.2f,0.2f,1),
                  COL_BORDER);
        drawTextCentered("END TURN", _endTurnBtn.Center().x, _endTurnBtn.y + 14, 0.5f,
                         active ? COL_WHITE : COL_DIM);
    }

    void drawLog() {
        auto* g = GraphicsSubsystem::Get();
        const auto& log = _mgr->log();
        float y = 78;
        for (const auto& line : log) {
            g->DrawText(_font, line, 20, y, 0.42f, vec4(0.8f,0.8f,0.95f,0.9f));
            y += 18;
        }
    }

    void drawBanner(const std::string& text, vec4 col) {
        auto* g = GraphicsSubsystem::Get();
        g->DrawQuad(W * 0.5f, H * 0.42f, W, 70, 0, vec4(0,0,0,0.55f));
        drawTextCentered(text, W * 0.5f, H * 0.42f - 16, 1.1f, col);
    }

    void drawReward() {
        auto* g = GraphicsSubsystem::Get();
        g->DrawQuad(W * 0.5f, H * 0.5f, W, H, 0, vec4(0,0,0,0.65f));
        drawTextCentered("Choose a card to add to your deck", W * 0.5f, 140, 0.8f, COL_SELECT);

        const auto& rc = _mgr->rewardCards();

        if (_rewardRects.size() < rc.size()) {
            _rewardRects.clear();
            const float CW = 116.0f, CH = 166.0f;
            int rn = static_cast<int>(rc.size());
            float rtotal = rn * CW + std::max(0, rn - 1) * 40.0f;
            float rstart = (W - rtotal) * 0.5f;
            for (int i = 0; i < rn; ++i)
                _rewardRects.push_back({ rstart + i * (CW + 40.0f), H * 0.5f - CH * 0.5f, CW, CH });
        }

        for (int i = 0; i < static_cast<int>(rc.size()); ++i) {
            const CardDef& card = GetCard(rc[i]);
            Rect r = _rewardRects[i];
            vec4 base;
            switch (card.kind) {
                case CardKind::Attack: base = vec4(0.45f,0.22f,0.22f,1); break;
                case CardKind::Skill:  base = vec4(0.22f,0.30f,0.45f,1); break;
                case CardKind::Power:  base = vec4(0.45f,0.38f,0.18f,1); break;
            }
            bool hov = r.Contains(input.GetMousePosition() / Window::Get()->GetDPI());
            drawPanel(r, base, hov ? COL_SELECT : COL_BORDER);
            g->DrawCircle(r.x + 16, r.y + 16, 14, vec4(0.15f,0.15f,0.25f,1));
            drawTextCentered(std::to_string(card.cost), r.x + 16, r.y + 6, 0.5f, COL_WHITE);
            drawTextCentered(card.name, r.Center().x, r.y + 34, 0.42f, COL_WHITE);
            drawWrapped(card.text, r.x + 8, r.y + 70, r.w - 16, 0.36f, COL_WHITE);
            drawTextCentered(fmt::format("[{}]", i + 1), r.Center().x, r.y + r.h - 22,
                             0.42f, COL_DIM);
        }
        drawTextCentered("Press SPACE to skip", W * 0.5f, H * 0.5f + 120, 0.5f, COL_DIM);
    }

    void drawEndScreen(const std::string& text, vec4 col) {
        auto* g = GraphicsSubsystem::Get();
        g->DrawQuad(W * 0.5f, H * 0.5f, W, H, 0, vec4(0,0,0,0.7f));
        drawTextCentered(text, W * 0.5f, H * 0.5f - 40, 1.6f, col);
        drawTextCentered("Press R to start a new run", W * 0.5f, H * 0.5f + 30, 0.6f, COL_WHITE);
    }
};

// ============================================================================
//  entry point
// ============================================================================
int main(int /*argc*/, char* /*argv*/[]) {
    CardBattleGame game({
        .windowTitle        = "Card Battle",
        .windowWidth        = 1280,
        .windowHeight       = 720,
        .enablePhysics3D    = false,
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
