#include "rpg_game.hpp"
#include <Atmospheric/graphics_server.hpp>
#include <Atmospheric/gfx_factory.hpp>
#include <Atmospheric/input.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

using glm::vec2;
using glm::vec4;

// ============================================================================
// Construction
// ============================================================================

RPGGame::RPGGame() : Application(AppConfig{
    .windowTitle    = "RPG Demo",
    .windowWidth    = 800,
    .windowHeight   = 600,
    .enableGraphics3D = false,
    .enablePhysics3D  = false,
}) {}

AnimClip RPGGame::MakeClip(int row, std::vector<int> cols, float dur, bool loop) {
    AnimClip c; c.loop = loop;
    for (int col : cols) c.frames.push_back({col, row, dur});
    return c;
}

// ============================================================================
// OnInit
// ============================================================================

void RPGGame::OnInit() {
    _screenW = 800; _screenH = 600;
    GoScene("main", [this]{ OnLoad(); });
}

// ============================================================================
// OnLoad
// ============================================================================

void RPGGame::OnLoad() {
    auto* gfx = GetGraphicsServer();
    _fontID = gfx->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 20.0f);

    // Tileset (4×2, 32px)
    constexpr int TS=32, TC=4, TR=2;
    _tilesetTex = GfxFactory::UploadTexture2D(
        MakeColorSheetPixels(TS, TC, TR, {
            {80,120,60},{60,50,40},{100,100,110},{40,80,130},
            {120,90,50},{80,80,80},{60,100,50},{180,160,120},
        }).data(), TC*TS, TR*TS);

    // Tilemap
    const std::vector<std::string> MAP = {
        "3333333333333333333333333",
        "3111111111111111111111113",
        "3111111111222111111111113",
        "3111221111222111113311113",
        "3111221111111111113311113",
        "3111111111111111111111113",
        "3111111111111111111111113",
        "3111111111111111111111113",
        "3133111111111111111331113",
        "3133111111111111111331113",
        "3111111111111111111111113",
        "3111111222211111122111113",
        "3111111222211111122111113",
        "3111111111111111111111113",
        "3111111111111111111111113",
        "3111441111111111114411113",
        "3111441111111111114411113",
        "3111111111111111111111113",
        "3111111111111111111111113",
        "3333333333333333333333333",
    };
    Tilemap2DData md;
    md.width = (int)MAP[0].size(); md.height = (int)MAP.size();
    md.tileSize = TS; md.tilesetCols = TC; md.tilesetRows = TR;
    md.solid = {3,4};
    for (const auto& row : MAP) for (char c : row) md.tiles.push_back(c-'0');
    _tilemap = std::make_unique<Tilemap2D>(md, _tilesetTex);

    // Character sheets (4×2 of 24px)
    constexpr int CS=24, CC=4, CR=2;
    _playerTex = GfxFactory::UploadTexture2D(
        MakeColorSheetPixels(CS,CC,CR,{
            {70,120,200},{60,110,190},{80,130,210},{65,115,195},
            {50,100,180},{75,125,205},{85,140,215},{55,105,185},
        }).data(), CC*CS, CR*CS);
    _enemyTex = GfxFactory::UploadTexture2D(
        MakeColorSheetPixels(CS,CC,CR,{
            {200,60,60},{180,50,50},{210,70,70},{190,55,55},
            {220,80,80},{170,45,45},{200,65,65},{185,55,55},
        }).data(), CC*CS, CR*CS);
    const uint8_t npcPx[4]={180,200,80,255};
    _npcTex = GfxFactory::UploadTexture2D(npcPx,1,1);

    // Player
    _player.x = 3*TS; _player.y = 3*TS; _player.w = _player.h = CS;
    _player.initSkills(); _player.initItems();
    _playerAnim.addClip("idle",   MakeClip(0,{0,1},0.4f));
    _playerAnim.addClip("walk",   MakeClip(0,{0,1,2,3},0.12f));
    _playerAnim.addClip("attack", MakeClip(1,{0,1,2},0.1f,false));
    _playerAnim.play("idle");

    // Battle state: link player stats
    _battle.playerStats = &_player.stats;
    _battle.level = 1; _battle.exp = 0; _battle.expToNext = 20; _battle.gold = 0;

    // Enemies — define archetypes
    auto makeSlime = [&](float ex, float ey) {
        EnemyDef def;
        def.name = "Slime";
        def.stats = {30,30,0,0,8,5,6,};
        def.expReward = 8; def.goldReward = 3;
        def.chooseAction = [](const Stats&, const Stats&) { return -1; }; // always attack
        return Enemy(ex, ey, std::move(def));
    };
    auto makeGoblin = [&](float ex, float ey) {
        EnemyDef def;
        def.name = "Goblin";
        def.stats = {45,45,0,0,12,6,10};
        def.expReward = 12; def.goldReward = 5;
        // Goblin: attack twice if hp > 50%
        def.chooseAction = [](const Stats& self, const Stats&) {
            return (self.hp > self.maxHp/2) ? -1 : -1;
        };
        return Enemy(ex, ey, std::move(def));
    };
    auto makeOrc = [&](float ex, float ey) {
        EnemyDef def;
        def.name = "Orc";
        def.stats = {70,70,0,0,16,10,7};
        def.expReward = 18; def.goldReward = 8;
        def.chooseAction = [](const Stats&, const Stats&) { return -1; };
        return Enemy(ex, ey, std::move(def));
    };

    const std::vector<std::pair<float,float>> spawns = {
        {8*TS,8*TS},{15*TS,12*TS},{20*TS,6*TS},{12*TS,16*TS},{5*TS,14*TS}
    };
    int idx = 0;
    for (auto [ex,ey] : spawns) {
        Enemy e = (idx%3==0) ? makeSlime(ex,ey)
                : (idx%3==1) ? makeGoblin(ex,ey)
                :               makeOrc(ex,ey);
        e.w = e.h = CS;
        _enemies.push_back(std::move(e));
        SpriteAnimator a;
        a.addClip("idle", MakeClip(0,{0,1},0.6f));
        a.addClip("walk", MakeClip(0,{0,1,2,3},0.15f));
        a.play("idle");
        _enemyAnims.push_back(std::move(a));
        idx++;
    }

    // NPCs
    _npcs.push_back(NPC(6*TS,6*TS,"Elder",{
        "Elder: Walk into monsters to battle them!",
        "Elder: Use Attack, Skills (costs MP) or Items.",
        "Elder: Defeating foes grants EXP and gold.",
        "Elder: Stay safe, adventurer!",
    }));
    _npcs.push_back(NPC(18*TS,15*TS,"Merchant",{
        "Merchant: I trade in rare goods.",
        "Merchant: (Shop not yet open)",
    }));

    // Lighting — mild darkness so tiles are still readable, lights pop in dark spots
    _lighting.ambientR = 0.25f; _lighting.ambientG = 0.28f;
    _lighting.ambientB = 0.40f; _lighting.ambientA = 1.0f;
}

// ============================================================================
// Camera
// ============================================================================

void RPGGame::CameraFollow(float tcx, float tcy) {
    _camX = tcx - _screenW*0.5f;
    _camY = tcy - _screenH*0.5f;
}
void RPGGame::CameraClamp(int mapW, int mapH) {
    _camX = std::clamp(_camX, 0.0f, (float)(mapW  - _screenW));
    _camY = std::clamp(_camY, 0.0f, (float)(mapH - _screenH));
}

// ============================================================================
// OnUpdate
// ============================================================================

void RPGGame::OnUpdate(float dt, float /*time*/) {
    _time += dt;
    _fadeIn = std::max(0.0f, _fadeIn - dt * 1.5f);

    if (_dialogTimer > 0) {
        _dialogTimer -= dt;
        if (_dialogTimer <= 0) _dialogText.clear();
    }

    switch (_mode) {
    case GameMode::Explore:
        UpdateExplore(dt);
        DrawExplore();
        DrawExploreHUD();
        if (!_dialogText.empty()) DrawDialog();
        break;

    case GameMode::BattleTransitionIn:
        _transition += dt * 3.0f;
        if (_transition >= 1.0f) {
            _transition = 1.0f;
            _mode = GameMode::Battle;
        }
        DrawExplore();
        {
            auto* gfx = GetGraphicsServer();
            gfx->DrawQuad(0,0,(float)_screenW,(float)_screenH,0,
                          vec4(0,0,0,_transition));
        }
        break;

    case GameMode::Battle:
        UpdateBattle(dt);
        DrawBattle();
        break;

    case GameMode::BattleTransitionOut:
        _transition -= dt * 3.0f;
        if (_transition <= 0.0f) {
            _transition = 0.0f;
            _mode = GameMode::Explore;
        }
        DrawExplore();
        {
            auto* gfx = GetGraphicsServer();
            gfx->DrawQuad(0,0,(float)_screenW,(float)_screenH,0,
                          vec4(0,0,0,_transition));
        }
        break;
    }

    // Global fade-in
    if (_fadeIn > 0.01f) {
        auto* gfx = GetGraphicsServer();
        gfx->DrawQuad(0,0,(float)_screenW,(float)_screenH,0,vec4(0,0,0,_fadeIn));
    }
}

// ============================================================================
// Exploration
// ============================================================================

void RPGGame::UpdateExplore(float dt) {
    auto* inp = GetInput();
    Player& p = _player;

    float ax=0, ay=0;
    if (inp->IsKeyDown(Key::LEFT)  || inp->IsKeyDown(Key::A)) ax -= 1;
    if (inp->IsKeyDown(Key::RIGHT) || inp->IsKeyDown(Key::D)) ax += 1;
    if (inp->IsKeyDown(Key::UP)    || inp->IsKeyDown(Key::W)) ay -= 1;
    if (inp->IsKeyDown(Key::DOWN)  || inp->IsKeyDown(Key::S)) ay += 1;

    // NPC interaction
    if (inp->IsKeyPressed(Key::E)) {
        for (auto& npc : _npcs) {
            float dx = npc.cx()-p.cx(), dy = npc.cy()-p.cy();
            if (std::sqrt(dx*dx+dy*dy) < npc.talkR) {
                _dialogText  = npc.dialogue[npc.dialogIdx % (int)npc.dialogue.size()];
                npc.dialogIdx++;
                _dialogTimer = 3.5f;
                break;
            }
        }
    }

    p.moving = (ax||ay);
    _playerAnim.play(p.moving ? "walk" : "idle");
    _playerAnim.update(dt);

    if (ax||ay) {
        float len = std::sqrt(ax*ax+ay*ay);
        float nx = ax/len, ny = ay/len;
        float newX = p.x + nx*p.speed*dt;
        float newY = p.y + ny*p.speed*dt;
        auto solid = [&](float wx,float wy){ return _tilemap->IsSolidWorld(wx,wy); };
        if (!solid(newX,p.y)&&!solid(newX+p.w-1,p.y)&&
            !solid(newX,p.y+p.h-1)&&!solid(newX+p.w-1,p.y+p.h-1)) p.x = newX;
        if (!solid(p.x,newY)&&!solid(p.x+p.w-1,newY)&&
            !solid(p.x,newY+p.h-1)&&!solid(p.x+p.w-1,p.y+p.h-1)) p.y = newY;
        if (ax) p.facing = (ax>0)?1.0f:-1.0f;
    }

    for (size_t i = 0; i < _enemies.size(); i++) {
        Enemy& e = _enemies[i];
        if (!e.alive) continue;
        _enemyAnims[i].update(dt);

        float dx = p.cx()-e.cx(), dy = p.cy()-e.cy();
        float dist = std::sqrt(dx*dx+dy*dy);

        // Aggro movement
        if (dist < e.aggroR) {
            e.aggro = true;
            float nx = dx/dist, ny = dy/dist;
            float newX = e.x + nx*30.0f*dt;
            float newY = e.y + ny*30.0f*dt;
            auto solid = [&](float wx,float wy){ return _tilemap->IsSolidWorld(wx,wy); };
            if (!solid(newX,e.y)&&!solid(newX+e.w-1,e.y)&&
                !solid(newX,e.y+e.h-1)&&!solid(newX+e.w-1,e.y+e.h-1)) e.x = newX;
            if (!solid(e.x,newY)&&!solid(e.x+e.w-1,newY)&&
                !solid(e.x,newY+e.h-1)&&!solid(e.x+e.w-1,e.y+e.h-1)) e.y = newY;
            _enemyAnims[i].play("walk");
        } else {
            e.aggro = false;
            _enemyAnims[i].play("idle");
        }

        // Contact → start battle
        if (AABBOverlaps(p.aabb(), e.aabb())) {
            StartBattle((int)i);
            return;
        }
    }

    // Camera
    CameraFollow(p.cx(), p.cy());
    CameraClamp(_tilemap->GetPixelWidth(), _tilemap->GetPixelHeight());

    // Lighting
    _lighting.lights.clear();
    _lighting.lights.push_back({p.cx()-_camX, p.cy()-_camY, 1.0f,0.9f,0.7f, 120.0f, 1.5f});
    for (const auto& e : _enemies) {
        if (!e.alive||!e.aggro) continue;
        float dx=e.cx()-p.cx(), dy=e.cy()-p.cy();
        if (dx*dx+dy*dy > 250.0f*250.0f) continue;
        _lighting.lights.push_back({e.cx()-_camX,e.cy()-_camY,1,0.3f,0.2f,80,0.7f});
    }
}

void RPGGame::DrawExplore() {
    auto* gfx = GetGraphicsServer();
    float camX = _camX, camY = _camY;

    _tilemap->Draw(gfx, camX, camY, _screenW, _screenH);

    for (const auto& npc : _npcs) {
        float sx = npc.x-camX, sy = npc.y-camY;
        gfx->DrawTexturedQuad(sx+npc.w*0.5f, sy+npc.h*0.5f, npc.w, npc.h, 0, _npcTex, vec4(1));
    }

    constexpr int CCOLS=4, CROWS=2;
    for (size_t i=0; i<_enemies.size(); i++) {
        if (!_enemies[i].alive) continue;
        const Enemy& e = _enemies[i];
        auto [fc,fr] = _enemyAnims[i].currentFrame();
        float sx=e.x-camX, sy=e.y-camY;
        vec2 uv0{(float)fc/CCOLS,(float)fr/CROWS};
        vec2 uv1{(float)(fc+1)/CCOLS,(float)(fr+1)/CROWS};
        gfx->DrawSprite2D(sx, sy, e.w, e.h, _enemyTex, uv0, uv1);
    }

    {
        auto [fc,fr] = _playerAnim.currentFrame();
        float sx=_player.x-camX, sy=_player.y-camY;
        vec2 uv0{(float)fc/CCOLS,(float)fr/CROWS};
        vec2 uv1{(float)(fc+1)/CCOLS,(float)(fr+1)/CROWS};
        gfx->DrawSprite2D(sx, sy, _player.w, _player.h, _playerTex, uv0, uv1);
    }

    _lighting.Apply(gfx, _screenW, _screenH);
}

void RPGGame::DrawExploreHUD() {
    auto* gfx = GetGraphicsServer();
    const Stats& s = _player.stats;

    DrawPanel(8, 8, 160, 50);
    gfx->DrawText(_fontID, fmt::format("LV{} EXP:{}/{}", _battle.level, _battle.exp, _battle.expToNext),
                  14, 12, 0.5f, vec4(0.9f,0.85f,0.6f,1));
    DrawHPBar(14, 28, 140, 10, s.hp, s.maxHp);
    DrawMPBar(14, 42, 140, 8,  s.mp, s.maxMp);

    gfx->DrawText(_fontID,
        fmt::format("Gold:{}", _battle.gold),
        14, (float)_screenH-36, 0.55f, vec4(1,0.9f,0.3f,1));
    gfx->DrawText(_fontID,
        "WASD:move  E:talk  walk into enemy to battle",
        14, (float)_screenH-20, 0.45f, vec4(0.7f,0.7f,0.7f,0.8f));
}

// ============================================================================
// Battle start / end
// ============================================================================

void RPGGame::StartBattle(int enemyIdx) {
    _mode = GameMode::BattleTransitionIn;
    _transition = 0.0f;

    Enemy& e = _enemies[enemyIdx];
    e.resetBattleStats();

    _battle.phase = BattlePhase::PlayerMenu;
    _battle.menuSel = 0; _battle.skillSel = 0; _battle.itemSel = 0;
    _battle.targetIdx = 0;
    _battle.actionTimer = 0.0f;
    _battle.log.clear();
    _battle.expGained = 0; _battle.goldGained = 0;

    _battle.enemyStats.clear();
    _battle.enemyNames.clear();
    _battle.enemyIndices.clear();
    _battle.enemyStats.push_back(e.battle);
    _battle.enemyNames.push_back(e.def.name);
    _battle.enemyIndices.push_back(enemyIdx);

    // Push enemy slightly away so contact doesn't re-trigger
    float dx = e.cx()-_player.cx(), dy = e.cy()-_player.cy();
    float len = std::sqrt(dx*dx+dy*dy);
    if (len > 0) { e.x += dx/len*32; e.y += dy/len*32; }

    _battle.pushLog(fmt::format("A {} appeared!", e.def.name));

    // SPD comparison for turn order
    _battle.playerFirst = (_player.stats.spd >= e.def.stats.spd);
}

void RPGGame::EndBattle(bool victory) {
    if (victory) {
        // Apply exp/gold
        _battle.exp  += _battle.expGained;
        _battle.gold += _battle.goldGained;

        // Level up
        while (_battle.exp >= _battle.expToNext) {
            _battle.exp -= _battle.expToNext;
            _battle.level++;
            _battle.expToNext = (int)(_battle.expToNext * 1.5f);
            // Stat increase
            _player.stats.maxHp  += 8;
            _player.stats.maxMp  += 4;
            _player.stats.atk    += 2;
            _player.stats.def    += 1;
            _player.stats.spd    += 1;
            _player.stats.hp = _player.stats.maxHp;
            _player.stats.mp = _player.stats.maxMp;
            _battle.pushLog(fmt::format("Level up! Now LV{}!", _battle.level));
        }

        // Mark world enemy dead
        for (int idx : _battle.enemyIndices) {
            if (idx >= 0 && idx < (int)_enemies.size())
                _enemies[idx].alive = false;
        }
    }

    _mode = GameMode::BattleTransitionOut;
    _transition = 1.0f;
}

// ============================================================================
// Battle update
// ============================================================================

int RPGGame::CalcDamage(int atk, int def) {
    std::uniform_int_distribution<int> jitter(-3,3);
    int dmg = std::max(1, atk - def/2 + jitter(_rng));
    return dmg;
}

void RPGGame::ExecutePlayerAttack() {
    Stats& es = _battle.enemyStats[_battle.targetIdx];
    int dmg = CalcDamage(_player.stats.atk, es.def);
    es.takeDamage(dmg);
    _shakeMag = 4.0f; _shakeTimer = 0.3f;
    _battle.pushLog(fmt::format("You attack {} for {} dmg!",
        _battle.enemyNames[_battle.targetIdx], dmg));
    _battle.phase = BattlePhase::EnemyAction;
    _battle.actionTimer = 0.8f;
}

void RPGGame::ExecutePlayerSkill(int idx) {
    if (idx >= (int)_player.skills.size()) return;
    Skill& sk = _player.skills[idx];
    if (_player.stats.mp < sk.mpCost) {
        _battle.pushLog("Not enough MP!");
        _battle.phase = BattlePhase::PlayerMenu;
        return;
    }
    _player.stats.mp -= sk.mpCost;

    if (sk.target == SkillTarget::AllEnemies) {
        int total = 0;
        for (auto& es : _battle.enemyStats) {
            if (es.isDead()) continue;
            int v = sk.calc(_player.stats, es);
            es.takeDamage(v); total += v;
        }
        _battle.pushLog(fmt::format("{} hits all for ~{} dmg!", sk.name, total/(int)_battle.enemyStats.size()));
    } else if (sk.target == SkillTarget::Self) {
        int v = sk.calc(_player.stats, _player.stats);
        _player.stats.heal(-v); // calc returns negative for heal
        _battle.pushLog(fmt::format("{}: restored {} HP!", sk.name, -v));
    } else {
        Stats& es = _battle.enemyStats[_battle.targetIdx];
        int v = sk.calc(_player.stats, es);
        es.takeDamage(v);
        _battle.pushLog(fmt::format("{} deals {} dmg!", sk.name, v));
    }
    _shakeMag = 6.0f; _shakeTimer = 0.3f;
    _battle.phase = BattlePhase::EnemyAction;
    _battle.actionTimer = 0.8f;
}

void RPGGame::ExecutePlayerItem(int idx) {
    if (idx >= (int)_player.items.size()) return;
    Item& it = _player.items[idx];
    if (it.count <= 0) { _battle.pushLog("None left!"); return; }
    it.count--;
    if (it.effect == ItemEffect::HealHP) {
        _player.stats.heal(it.amount);
        _battle.pushLog(fmt::format("{}: recovered {} HP!", it.name, it.amount));
    } else {
        _player.stats.restoreMp(it.amount);
        _battle.pushLog(fmt::format("{}: recovered {} MP!", it.name, it.amount));
    }
    _battle.phase = BattlePhase::EnemyAction;
    _battle.actionTimer = 0.8f;
}

void RPGGame::ExecuteEnemyTurn() {
    // Find first living enemy
    int actorIdx = -1;
    for (int i=0; i<(int)_battle.enemyStats.size(); i++)
        if (!_battle.enemyStats[i].isDead()) { actorIdx = i; break; }
    if (actorIdx < 0) return;

    const Enemy& worldE = _enemies[_battle.enemyIndices[actorIdx]];
    Stats& es = _battle.enemyStats[actorIdx];
    int dmg = CalcDamage(worldE.def.stats.atk, _player.stats.def);
    _player.stats.takeDamage(dmg);
    _battle.pushLog(fmt::format("{} attacks for {} dmg!", worldE.def.name, dmg));
    _battle.phase = BattlePhase::PlayerMenu;
}

void RPGGame::UpdateBattle(float dt) {
    auto* inp = GetInput();
    BattleState& b = _battle;

    // Screen shake
    if (_shakeTimer > 0) _shakeTimer -= dt;
    else _shakeMag = 0;

    b.tickLog(dt);

    switch (b.phase) {
    case BattlePhase::PlayerMenu: {
        int count = (int)BattleMenuSel::COUNT;
        if (inp->IsKeyPressed(Key::UP)   || inp->IsKeyPressed(Key::W)) b.menuSel = (b.menuSel+count-1)%count;
        if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.menuSel = (b.menuSel+1)%count;
        if (inp->IsKeyPressed(Key::Z) || inp->IsKeyPressed(Key::ENTER)) {
            switch ((BattleMenuSel)b.menuSel) {
            case BattleMenuSel::Attack: ExecutePlayerAttack(); break;
            case BattleMenuSel::Skills: b.phase = BattlePhase::PlayerSkillMenu; b.skillSel=0; break;
            case BattleMenuSel::Items:  b.phase = BattlePhase::PlayerItemMenu;  b.itemSel=0;  break;
            case BattleMenuSel::Run:    b.phase = BattlePhase::Flee; b.actionTimer = 0.5f; break;
            default: break;
            }
        }
        break;
    }
    case BattlePhase::PlayerSkillMenu: {
        int count = (int)_player.skills.size();
        if (inp->IsKeyPressed(Key::UP)   || inp->IsKeyPressed(Key::W)) b.skillSel=(b.skillSel+count-1)%count;
        if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.skillSel=(b.skillSel+1)%count;
        if (inp->IsKeyPressed(Key::Z) || inp->IsKeyPressed(Key::ENTER)) ExecutePlayerSkill(b.skillSel);
        if (inp->IsKeyPressed(Key::X) || inp->IsKeyPressed(Key::ESCAPE)) b.phase = BattlePhase::PlayerMenu;
        break;
    }
    case BattlePhase::PlayerItemMenu: {
        int count = (int)_player.items.size();
        if (inp->IsKeyPressed(Key::UP)   || inp->IsKeyPressed(Key::W)) b.itemSel=(b.itemSel+count-1)%count;
        if (inp->IsKeyPressed(Key::DOWN) || inp->IsKeyPressed(Key::S)) b.itemSel=(b.itemSel+1)%count;
        if (inp->IsKeyPressed(Key::Z) || inp->IsKeyPressed(Key::ENTER)) ExecutePlayerItem(b.itemSel);
        if (inp->IsKeyPressed(Key::X) || inp->IsKeyPressed(Key::ESCAPE)) b.phase = BattlePhase::PlayerMenu;
        break;
    }
    case BattlePhase::EnemyAction:
        b.actionTimer -= dt;
        if (b.actionTimer <= 0) {
            if (b.allEnemiesDead()) {
                // Tally rewards
                for (int idx : b.enemyIndices) {
                    if (idx<(int)_enemies.size()) {
                        b.expGained  += _enemies[idx].def.expReward;
                        b.goldGained += _enemies[idx].def.goldReward;
                    }
                }
                b.pushLog(fmt::format("Victory! +{} EXP +{} Gold", b.expGained, b.goldGained));
                b.phase = BattlePhase::Victory;
                b.actionTimer = 2.5f;
            } else if (_player.stats.isDead()) {
                b.pushLog("You were defeated...");
                b.phase = BattlePhase::Defeat;
                b.actionTimer = 2.5f;
            } else {
                ExecuteEnemyTurn();
            }
        }
        break;
    case BattlePhase::Victory:
        b.actionTimer -= dt;
        if (b.actionTimer <= 0) EndBattle(true);
        break;
    case BattlePhase::Defeat:
        b.actionTimer -= dt;
        if (b.actionTimer <= 0) {
            // Revive with 1 HP and go back to explore
            _player.stats.hp = 1;
            EndBattle(false);
        }
        break;
    case BattlePhase::Flee:
        b.actionTimer -= dt;
        if (b.actionTimer <= 0) {
            b.pushLog("Escaped!");
            EndBattle(false);
        }
        break;
    default: break;
    }
}

// ============================================================================
// Battle draw
// ============================================================================

void RPGGame::DrawBattle() {
    auto* gfx = GetGraphicsServer();
    const float W = (float)_screenW, H = (float)_screenH;

    // Background
    gfx->DrawQuad(W*0.5f, H*0.5f, W, H, 0, vec4(0.08f,0.06f,0.14f,1));

    // Subtle grid lines
    for (int i=0; i<8; i++) {
        float y = i * H/8.0f;
        gfx->DrawRect(0, y, W, 1, vec4(0.15f,0.12f,0.22f,0.5f));
    }

    // Screen shake offset
    float shakeX = 0, shakeY = 0;
    if (_shakeTimer > 0) {
        std::uniform_real_distribution<float> d(-_shakeMag, _shakeMag);
        shakeX = d(_rng); shakeY = d(_rng);
    }

    // Enemy sprites (right side)
    constexpr int CCOLS=4, CROWS=2;
    for (int i=0; i<(int)_battle.enemyStats.size(); i++) {
        const Stats& es = _battle.enemyStats[i];
        if (es.isDead()) continue;
        float ex = W*0.65f + i*100 + shakeX;
        float ey = H*0.35f + shakeY;
        float sz = 72;
        vec2 uv0{0,0}, uv1{1.0f/CCOLS, 1.0f/CROWS};
        gfx->DrawSprite2D(ex-sz*0.5f, ey-sz*0.5f, sz, sz, _enemyTex, uv0, uv1,
                           (_battle.targetIdx==i) ? vec4(1,0.7f,0.7f,1) : vec4(1));

        // Enemy name + HP bar above
        gfx->DrawText(_fontID, _battle.enemyNames[i], ex-30, ey-sz*0.5f-28, 0.6f, vec4(1,0.8f,0.8f,1));
        DrawHPBar(ex-50, ey-sz*0.5f-14, 100, 8, es.hp, es.maxHp, vec4(0.9f,0.2f,0.2f,1));
    }

    // Player sprite (left side)
    {
        auto [fc,fr] = _playerAnim.currentFrame();
        float px = W*0.25f, py = H*0.42f, sz = 72;
        vec2 uv0{(float)fc/CCOLS,(float)fr/CROWS};
        vec2 uv1{(float)(fc+1)/CCOLS,(float)(fr+1)/CROWS};
        gfx->DrawSprite2D(px-sz*0.5f, py-sz*0.5f, sz, sz, _playerTex, uv0, uv1);
    }

    DrawBattleStats();
    DrawBattleMenu();
    DrawBattleLog();

    // Phase overlay text
    if (_battle.phase == BattlePhase::Victory) {
        DrawPanel(W*0.5f-120, H*0.5f-30, 240, 60, vec4(0.05f,0.2f,0.05f,0.95f));
        gfx->DrawText(_fontID, "VICTORY!", W*0.5f-55, H*0.5f-14, 1.0f, vec4(0.3f,1,0.4f,1));
    } else if (_battle.phase == BattlePhase::Defeat) {
        DrawPanel(W*0.5f-120, H*0.5f-30, 240, 60, vec4(0.2f,0.03f,0.03f,0.95f));
        gfx->DrawText(_fontID, "DEFEATED", W*0.5f-55, H*0.5f-14, 1.0f, vec4(1,0.3f,0.3f,1));
    }
}

void RPGGame::DrawBattleStats() {
    auto* gfx = GetGraphicsServer();
    const float W = (float)_screenW, H = (float)_screenH;
    const Stats& s = _player.stats;

    DrawPanel(8, H-110, 200, 100);
    gfx->DrawText(_fontID, fmt::format("LV{}", _battle.level), 16, H-106, 0.55f, vec4(1,0.9f,0.5f,1));
    gfx->DrawText(_fontID, fmt::format("HP {}/{}", s.hp, s.maxHp), 16, H-90,  0.55f, vec4(0.7f,1,0.7f,1));
    DrawHPBar(16, H-78, 180, 10, s.hp, s.maxHp);
    gfx->DrawText(_fontID, fmt::format("MP {}/{}", s.mp, s.maxMp), 16, H-64,  0.55f, vec4(0.5f,0.7f,1,1));
    DrawMPBar(16, H-52, 180, 10, s.mp, s.maxMp);
    gfx->DrawText(_fontID, fmt::format("ATK:{} DEF:{}", s.atk, s.def), 16, H-36, 0.5f, vec4(0.8f,0.8f,0.8f,0.9f));
}

void RPGGame::DrawBattleMenu() {
    auto* gfx = GetGraphicsServer();
    const float W = (float)_screenW, H = (float)_screenH;
    const BattleState& b = _battle;

    if (b.phase == BattlePhase::PlayerMenu) {
        DrawPanel(W-180, H-130, 170, 120);
        const char* labels[] = {"Attack","Skills","Items","Run"};
        for (int i=0; i<4; i++) {
            bool sel = (b.menuSel == i);
            gfx->DrawText(_fontID, fmt::format("{} {}", sel?"▶":" ", labels[i]),
                           W-170, H-124+i*26, 0.65f,
                           sel ? vec4(1,1,0.3f,1) : vec4(0.85f,0.85f,0.9f,1));
        }
        gfx->DrawText(_fontID, "Z:confirm", W-170, H-18, 0.45f, vec4(0.5f,0.5f,0.6f,1));
    }
    else if (b.phase == BattlePhase::PlayerSkillMenu) {
        DrawPanel(W-220, H-200, 210, (int)_player.skills.size()*30+40);
        gfx->DrawText(_fontID, "─ Skills ─", W-210, H-194, 0.55f, vec4(0.7f,0.7f,1,1));
        for (int i=0; i<(int)_player.skills.size(); i++) {
            bool sel = (b.skillSel==i);
            const Skill& sk = _player.skills[i];
            gfx->DrawText(_fontID,
                fmt::format("{}{} ({}MP)", sel?"▶":" ", sk.name, sk.mpCost),
                W-210, H-178+i*28, 0.6f,
                sel ? vec4(1,1,0.3f,1) : vec4(0.85f,0.85f,0.9f,1));
        }
        gfx->DrawText(_fontID, "X:back", W-210, H-16, 0.45f, vec4(0.5f,0.5f,0.6f,1));
    }
    else if (b.phase == BattlePhase::PlayerItemMenu) {
        DrawPanel(W-220, H-200, 210, (int)_player.items.size()*30+40);
        gfx->DrawText(_fontID, "─ Items ─", W-210, H-194, 0.55f, vec4(0.7f,1,0.7f,1));
        for (int i=0; i<(int)_player.items.size(); i++) {
            bool sel = (b.itemSel==i);
            const Item& it = _player.items[i];
            gfx->DrawText(_fontID,
                fmt::format("{}{} x{}", sel?"▶":" ", it.name, it.count),
                W-210, H-178+i*28, 0.6f,
                sel ? vec4(1,1,0.3f,1) : (it.count>0 ? vec4(0.85f,0.85f,0.9f,1) : vec4(0.4f,0.4f,0.4f,1)));
        }
        gfx->DrawText(_fontID, "X:back", W-210, H-16, 0.45f, vec4(0.5f,0.5f,0.6f,1));
    }
}

void RPGGame::DrawBattleLog() {
    auto* gfx = GetGraphicsServer();
    const float W = (float)_screenW, H = (float)_screenH;

    DrawPanel(220, H-130, W-440, 120);
    int shown = std::min((int)_battle.log.size(), 4);
    for (int i=0; i<shown; i++) {
        const auto& entry = _battle.log[_battle.log.size()-shown+i];
        float alpha = std::min(1.0f, entry.ttl);
        gfx->DrawText(_fontID, entry.text, 232, H-124+i*26, 0.58f, vec4(0.9f,0.9f,1,alpha));
    }
}

// ============================================================================
// UI helpers
// ============================================================================

void RPGGame::DrawPanel(float x, float y, float w, float h, vec4 bg) {
    auto* gfx = GetGraphicsServer();
    gfx->DrawQuad(x+w*0.5f, y+h*0.5f, w, h, 0, bg);
    gfx->DrawRect(x, y, w, h, vec4(0.35f,0.35f,0.55f,0.8f));
}

void RPGGame::DrawHPBar(float x, float y, float w, float h, int hp, int maxHp, vec4 color) {
    auto* gfx = GetGraphicsServer();
    gfx->DrawQuad(x+w*0.5f, y+h*0.5f, w, h, 0, vec4(0.1f,0.1f,0.1f,0.85f));
    float ratio = maxHp>0 ? (float)hp/maxHp : 0;
    float fw = (w-2)*ratio;
    if (fw > 0)
        gfx->DrawQuad(x+1+fw*0.5f, y+1+(h-2)*0.5f, fw, h-2, 0, color);
}

void RPGGame::DrawMPBar(float x, float y, float w, float h, int mp, int maxMp) {
    auto* gfx = GetGraphicsServer();
    gfx->DrawQuad(x+w*0.5f, y+h*0.5f, w, h, 0, vec4(0.1f,0.1f,0.1f,0.85f));
    float ratio = maxMp>0 ? (float)mp/maxMp : 0;
    float fw = (w-2)*ratio;
    if (fw > 0)
        gfx->DrawQuad(x+1+fw*0.5f, y+1+(h-2)*0.5f, fw, h-2, 0, vec4(0.2f,0.4f,1,1));
}

void RPGGame::DrawDialog() {
    auto* gfx = GetGraphicsServer();
    const float W=(float)_screenW, H=(float)_screenH;
    const float bx=40, bh=70, by=H-bh-20, bw=W-80;
    DrawPanel(bx, by, bw, bh);
    gfx->DrawQuad(bx+bw*0.5f, by+1.5f, bw, 3, 0, vec4(0.5f,0.8f,1,1));
    if (_fontID)
        gfx->DrawText(_fontID, _dialogText, bx+12, by+14, 0.7f, vec4(0.9f,0.9f,1,1));
}
