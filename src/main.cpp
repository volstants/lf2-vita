#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <psp2/kernel/processmgr.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <deque>

#include "engine/types.hpp"
#include "engine/render.hpp"
#include "engine/dat.hpp"
#include "engine/fighter.hpp"
#include "engine/object.hpp"
#include "characters/player.hpp"
#include "characters/enemy.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Roster & asset banks — everything is loaded from the original .dat files.
//  Character sheets/faces/ball sheets were batch-converted from the original
//  BMPs (tools/bmp2png.py); ALL of them use black as the transparent key.
// ─────────────────────────────────────────────────────────────────────────────
static const char* ROSTER[] = { "dennis", "davis", "woody", "firen",
                                "freeze", "rudolf", "louis", "henry" };
constexpr int ROSTER_N = 8;

// "sprite\sys\dennis_0.bmp" → "app0:/assets/dennis_0.png"
static std::string sheetAsset(const std::string& datPath) {
    size_t slash = datPath.find_last_of("\\/");
    std::string base = (slash == std::string::npos) ? datPath : datPath.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return "app0:/assets/" + base + ".png";
}

struct CharAssets {
    dat::File data;
    std::vector<SDL_Texture*> sheets;   // one per header file() entry, in order
    SDL_Texture* face = nullptr;
    bool texLoaded = false;

    void loadTextures(SDL_Renderer* r, const char* name) {
        if (texLoaded) return;
        sheets.clear();
        for (const auto& s : data.header.files)
            sheets.push_back(loadTex(r, sheetAsset(s.path).c_str(), true, 0, 0, 0));
        face = loadTex(r, (std::string("app0:/assets/") + name + "_f.png").c_str(), false);
        texLoaded = true;
    }
};
static CharAssets g_chars[ROSTER_N];
static dat::Index g_index;              // data.txt: oid → file

// Projectile bank, cached by oid, resolved through data.txt at first spawn.
struct ObjAssets {
    int oid = -1;
    dat::File data;
    std::vector<SDL_Texture*> sheets;
};
// DEQUE, not vector: live Objects hold `&entry.data` pointers, and a vector
// reallocation on the next spawn would dangle every one of them (crash).
static std::deque<ObjAssets> g_objBank;

// Index of an entry, since deque has no contiguous storage for pointer math.
static int objBankIndex(const ObjAssets* oa) {
    for (size_t i = 0; i < g_objBank.size(); ++i) if (&g_objBank[i] == oa) return (int)i;
    return -1;
}

// Cache lookup. A miss loads the .dat AND its sheets from disk — far too slow to
// do inside the game loop (a special that fires an opoint would stall mid-swing,
// and Rudolf's opoint oid 5 is rudolf.dat itself: 3 sheets of 800×560). Failures
// are cached too: without that, an oid that can't load was re-read from disk on
// every single call. Use preloadObjAssets() at startup so play never hits disk.
static ObjAssets* objAssets(SDL_Renderer* r, int oid) {
    for (auto& o : g_objBank)
        if (o.oid == oid) return o.data.frames.empty() ? nullptr : &o;
    const dat::ObjectEntry* e = g_index.object(oid);
    ObjAssets oa; oa.oid = oid;
    if (e) {
        std::string path = e->file;                   // "data\dennis_ball.dat"
        for (auto& c : path) if (c == '\\') c = '/';
        oa.data = dat::load(("app0:/" + path).c_str());
        if (!oa.data.frames.empty())
            for (const auto& s : oa.data.header.files)
                oa.sheets.push_back(loadTex(r, sheetAsset(s.path).c_str(), true, 0, 0, 0));
    }
    g_objBank.push_back(std::move(oa));               // cache hits AND misses
    ObjAssets* back = &g_objBank.back();
    return back->data.frames.empty() ? nullptr : back;
}

// Warm the cache for every object a character can emit, so no spawn ever waits
// on the filesystem. Called once per character at load time.
static void preloadObjAssets(SDL_Renderer* r, const dat::File& d) {
    for (const auto& fr : d.frames)
        for (const auto& op : fr.opoints)
            if (op.oid > 0) objAssets(r, op.oid);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Generic fighter rendering — geometry comes from the .dat header (cell w/h,
//  cells-per-row) with the +1 grid-line stride the original sheets use.
// ─────────────────────────────────────────────────────────────────────────────
static void drawFighter(SDL_Renderer* r, const lf2::Fighter& f,
                        const std::vector<SDL_Texture*>& sheets, int camX,
                        Uint8 cr = 255, Uint8 cg = 255, Uint8 cb = 255)
{
    int ord, loc;
    if (!f.sheetLocal(ord, loc) || ord < 0 || ord >= (int)sheets.size() || !sheets[ord])
        return;
    const dat::SpriteSheet& s = f.data->header.files[ord];
    int row = s.row > 0 ? s.row : 1;
    SDL_Rect src = { (loc % row) * (s.w + 1), (loc / row) * (s.h + 1), s.w, s.h };
    float dx, dy;
    f.drawOrigin(dx, dy);
    SDL_Rect dst = { (int)dx - camX, (int)dy, s.w, s.h };
    SDL_SetTextureColorMod(sheets[ord], cr, cg, cb);
    SDL_RenderCopyEx(r, sheets[ord], &src, &dst, 0, nullptr,
                     f.facingRight ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL);
    SDL_SetTextureColorMod(sheets[ord], 255, 255, 255);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Collision helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline Box toBox(const lf2::WBox& w) {
    return { (int)w.x, (int)w.y, (int)w.w, (int)w.h };
}

struct HitInfo {
    Box box;
    int injury = 20, fall = -1, dvx = 0, rest = 8, zwidth = 15;
};
static bool fighterAttack(const lf2::Fighter& f, HitInfo& out) {
    bool found = false;
    f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
        if (it.kind == 0 && !found) {
            out.box    = toBox(wb);
            out.injury = it.injury > 0 ? it.injury : 20;
            out.fall   = it.fall;
            out.dvx    = it.dvx;
            out.rest   = it.vrest > 0 ? it.vrest : (it.arest > 0 ? it.arest : 8);
            // OpenLF2 (decompiled): z-band = itr->zwidth, default 15 when 0/unset;
            // hit requires abs(dz) < zwidth. Whirlwind moves set a wide zwidth.
            out.zwidth = it.zwidth > 0 ? it.zwidth : 15;
            found = true;
        }
    });
    return found;
}
static bool fighterBody(const lf2::Fighter& f, Box& out) {
    bool found = false;
    f.forEachBdy([&](const lf2::WBox& wb, const dat::Bdy&) {
        if (!found) { out = toBox(wb); found = true; }
    });
    return found;
}

// An attack box also hurts WEAPONS. Each weapon has <weapon_hp>; at 0 it breaks
// (F.LF weapon.js 'die'), which spawns the broken-weapon effect, oid 999.
static void damageObjects(SDL_Renderer* r, const HitInfo& hi, bool fromRight, int skipIdx);

// Obstacle test: does any grounded object present an itr of kind 14 (LF2's
// "blocking" box) where the player's body now is? Only weapon1.dat (stone) ships
// one, on its resting frame — knives and airborne/held weapons never block.
// The test is x/y + a z band, so jumping clears it for free: in the air the
// player's body box no longer overlaps the obstacle's ground-level box.
template <int N>
static bool blockedByObstacle(const lf2::Player& p, lf2::ObjectPool<N>& objs, int heldIdx) {
    Box pb;
    if (!fighterBody(p.f, pb)) return false;
    bool blocked = false;
    for (int i = 0; i < objs.SIZE; i++) {
        if (i == heldIdx) continue;
        lf2::Object& o = objs.objs[i];
        if (!o.active || o.held || o.thrown) continue;
        o.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
            if (it.kind != 14 || blocked) return;
            float zw = (float)(it.zwidth > 0 ? it.zwidth : 15);
            if (fabsf(o.f.z - p.z) >= zw) return;
            if (boxOverlap(toBox(wb), pb)) blocked = true;
        });
        if (blocked) break;
    }
    return blocked;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
struct InputState { bool L, R, U, D, atk, jmp, def, spc, start, any; };

static InputState readInput(SDL_Joystick* joy) {
    InputState in{};
    if (!joy) return in;
    Sint16 ax = SDL_JoystickGetAxis(joy, 0);
    Sint16 ay = SDL_JoystickGetAxis(joy, 1);
    if (ax < -DEADZONE) in.L = true;
    if (ax >  DEADZONE) in.R = true;
    if (ay < -DEADZONE) in.U = true;
    if (ay >  DEADZONE) in.D = true;
    if (SDL_JoystickGetButton(joy, BTN_LEFT))  in.L = true;
    if (SDL_JoystickGetButton(joy, BTN_RIGHT)) in.R = true;
    if (SDL_JoystickGetButton(joy, BTN_UP))    in.U = true;
    if (SDL_JoystickGetButton(joy, BTN_DOWN))  in.D = true;
    in.atk = SDL_JoystickGetButton(joy, BTN_ATTACK);
    in.jmp = SDL_JoystickGetButton(joy, BTN_JUMP);
    in.def = SDL_JoystickGetButton(joy, BTN_DEFEND);
    in.spc   = SDL_JoystickGetButton(joy, BTN_SPECIAL);
    in.start = SDL_JoystickGetButton(joy, BTN_START);
    in.any = in.atk || in.jmp || in.def || in.spc || in.L || in.R || in.U || in.D;
    return in;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Background — data-driven from bg.dat (dat::Background). Replaces the old
//  hardcoded Lion Forest renderer, which had no parallax and tiled the ground
//  by image width instead of the layer's `loop` spacing. See dat.hpp for the
//  bg.dat semantics; the draw here mirrors the game's own FUN_0041a250.
// ─────────────────────────────────────────────────────────────────────────────
struct Scene {
    dat::Background bg;
    std::vector<SDL_Texture*> layerTex;   // parallel to bg.layers; null for rect layers
    SDL_Texture* shadow = nullptr;

    void load(SDL_Renderer* r, const char* bgDatPath) {
        bg = dat::loadBackground(bgDatPath);
        layerTex.assign(bg.layers.size(), nullptr);
        for (size_t i = 0; i < bg.layers.size(); ++i) {
            const dat::BgLayer& L = bg.layers[i];
            if (L.isRect()) continue;                       // solid fill: no bitmap
            std::string png = sheetAsset(L.file);           // bg\sys\lf\x.bmp → app0:/assets/x.png
            // transparency:1 → colour-key black (LF2 backgrounds key on black).
            layerTex[i] = loadTex(r, png.c_str(), L.transparency != 0, 0, 0, 0);
        }
        if (!bg.shadow.empty())
            shadow = loadTex(r, sheetAsset(bg.shadow).c_str(), true, 0, 0, 0);
    }
};

static void renderBackground(SDL_Renderer* r, const Scene& sc, int camX) {
    SDL_SetRenderDrawColor(r, 111, 163, 218, 255);   // sky (not in bg.dat; port default)
    SDL_RenderClear(r);
    const dat::Background& bg = sc.bg;
    for (size_t i = 0; i < bg.layers.size(); ++i) {
        const dat::BgLayer& L = bg.layers[i];

        // Rect layer: a fixed-screen solid fill (no parallax in the original).
        // Widen to our 960px viewport so the ground covers the whole screen.
        if (L.isRect()) {
            int cr, cg, cb; dat::rectColor(L.rect, cr, cg, cb);
            int w = L.width > 0 ? L.width : SCREEN_W;
            if (L.x + w < SCREEN_W) w = SCREEN_W - L.x;
            fillRect(r, L.x, L.y, w, L.height, (Uint8)cr, (Uint8)cg, (Uint8)cb);
            continue;
        }

        SDL_Texture* t = sc.layerTex[i];
        if (!t) continue;
        int tw = 0, th = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
        if (tw <= 0) continue;
        int sx = bg.layerScreenX(L, camX);              // parallax-shifted screen x

        // Repeat rules. `loop > 0` = the data says tile (grass tufts, ground).
        // For loop == 0 the original draws ONE copy; that leaves a gap on our
        // 960-wide viewport (the layers are authored for LF2's 794). Only the
        // near-static backdrop (parallax width ≈ the 794 reference, e.g. the sky)
        // may repeat to fill — the mountain clumps (width 1100/1400) are single
        // pieces and tiling them stamped a repeated ridge across the sky.
        bool backdrop = (L.width <= dat::Background::PARALLAX_REF + 64);
        int step = (L.loop > 0) ? L.loop : tw;
        if (step <= 0) step = tw;
        if (L.loop <= 0 && !backdrop) {                 // single piece
            SDL_Rect d = { sx, L.y, tw, th };
            if (sx + tw > 0 && sx < SCREEN_W) SDL_RenderCopy(r, t, nullptr, &d);
            continue;
        }
        int start = sx;
        while (start > 0)        start -= step;         // first copy at/left of x=0
        while (start + tw < 0)   start += step;
        for (int dxs = start; dxs < SCREEN_W; dxs += step) {
            SDL_Rect d = { dxs, L.y, tw, th };
            SDL_RenderCopy(r, t, nullptr, &d);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game state
// ─────────────────────────────────────────────────────────────────────────────
static lf2::ObjectPool<24> g_objects;

struct EnemySlot { Enemy e; int rosterIdx = 3; int prevFrame = -1; };

// Spawn every kind-1 opoint of the frame the fighter JUST entered.
static void spawnOpoints(SDL_Renderer* r, const lf2::Fighter& f, int team) {
    const dat::Frame* fr = f.cur();
    if (!fr || fr->opoints.empty()) return;
    for (const dat::Opoint& op : fr->opoints) {
        if (op.kind != 1) continue;
        ObjAssets* oa = objAssets(r, op.oid);
        if (!oa) continue;
        lf2::Object* o = g_objects.alloc();
        if (!o) continue;
        float wx, wy;
        f.pointWorld(op.x, op.y, wx, wy);
        bool faceRight = (op.facing == 1) ? !f.facingRight : f.facingRight;
        float vx0 = faceRight ? (float)op.dvx : -(float)op.dvx;   // launch in facing dir
        o->spawn(&oa->data, objBankIndex(oa), wx, wy, f.z,
                 faceRight, op.action, team, vx0, (float)op.dvy);
        // Several "projectiles" are actually WEAPON objects in data.txt: Henry's
        // arrows (oid 201) and Rudolf's darts (oid 202) are type 1. Left at
        // weaponType 0 they took the ball path — no gravity (a negative opoint
        // dvy climbed forever) and the hit test (flying() || thrown) never
        // matched their weapon frames, so they passed straight through. Tag them
        // and mark them airborne: gravity arcs them and they can connect.
        // (Only types 1/2 — type 3 is a real projectile/effect and must keep the
        // straight-flight ball path.)
        const dat::ObjectEntry* oe = g_index.object(op.oid);
        int oty = oe ? oe->type : 0;
        if (oty == 1 || oty == 2) {
            o->weaponType = oty;
            o->thrown     = true;
            o->groundY    = f.z;       // floor line to land back on
            o->ephemeral  = true;      // must expire after landing, or the pool
                                       // fills with spent arrows and no special
                                       // can spawn anything ever again
        }
    }
}

// Apply an attack box to every weapon lying around: durability comes from the
// weapon's own <weapon_hp>, and at 0 it shatters into broken_weapon.dat (oid 999).
static void damageObjects(SDL_Renderer* r, const HitInfo& hi, bool fromRight, int skipIdx) {
    for (int i = 0; i < g_objects.SIZE; i++) {
        if (i == skipIdx) continue;                 // never your own held weapon
        lf2::Object& o = g_objects.objs[i];
        if (!o.active || o.weaponType <= 0 || o.rehit > 0) continue;
        bool hit = false;
        o.f.forEachBdy([&](const lf2::WBox& wb, const dat::Bdy&) {
            if (!hit && boxOverlap(hi.box, toBox(wb))) hit = true;
        });
        if (!hit) continue;
        o.rehit = hi.rest > 0 ? hi.rest : 8;
        if (!o.takeHit(hi.injury, fromRight)) continue;
        // Broke: swap it for the shatter effect at the same spot.
        float bx = o.f.x, by = o.f.y, bz = o.f.z;
        bool  br = o.f.facingRight;
        o.active = false;
        if (ObjAssets* oa = objAssets(r, 999)) {
            if (lf2::Object* fx = g_objects.alloc())
                fx->spawn(&oa->data, objBankIndex(oa), bx, by, bz, br, 0, /*team=*/0);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD
// ─────────────────────────────────────────────────────────────────────────────
static void renderHUD(SDL_Renderer* r, const lf2::Player& player, int playerIdx,
                      EnemySlot slots[])
{
    // LF2/F.LF layout: one light-blue band split into 4 panels per row, two rows
    // (LF2 shows 8 fighter slots). Each panel = portrait on the left, then a red
    // HP bar over a dark-red track and a blue MP bar over a dark-blue track.
    // Unused slots stay as empty recessed tracks instead of disappearing.
    constexpr int COLS    = 4;
    constexpr int ROW_H   = 56;
    constexpr int HUD_H   = ROW_H * 2;
    constexpr int SW_SLOT = SCREEN_W / COLS;
    constexpr int PORT    = 44;
    constexpr int BAR_X   = PORT + 8;
    constexpr int BAR_W   = SW_SLOT - BAR_X - 10;
    constexpr int BAR_H   = 12;

    fillRect(r, 0, 0, SCREEN_W, HUD_H, 92, 124, 190);          // panel
    fillRect(r, 0, HUD_H, SCREEN_W, 2, 20, 34, 78);            // bottom edge

    // Panel separators + recessed empty tracks for every slot of both rows.
    for (int row = 0; row < 2; row++) {
        for (int c = 0; c < COLS; c++) {
            int sx = c * SW_SLOT, sy = row * ROW_H;
            if (c) fillRect(r, sx - 1, sy + 2, 2, ROW_H - 4, 60, 88, 150);
            fillRect(r, sx + BAR_X - 1, sy + 13, BAR_W + 2, BAR_H + 2, 62, 90, 152);
            fillRect(r, sx + BAR_X - 1, sy + 31, BAR_W + 2, BAR_H + 2, 62, 90, 152);
        }
    }

    auto panel = [&](int slot, SDL_Texture* face, bool mirror,
                     int hp, int maxHp, int mp, int maxMp)
    {
        int sx = (slot % COLS) * SW_SLOT, sy = (slot / COLS) * ROW_H;
        fillRect(r, sx + 3, sy + 5, PORT + 2, PORT + 2, 20, 34, 78);   // portrait frame
        if (face) {
            SDL_Rect d = { sx + 4, sy + 6, PORT, PORT };
            SDL_RenderCopyEx(r, face, nullptr, &d, 0, nullptr,
                             mirror ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
        drawHpBar(r, sx + BAR_X, sy + 14, hp, maxHp, 214, 36, 36, BAR_W, BAR_H);
        drawHpBar(r, sx + BAR_X, sy + 32, mp, maxMp,  44, 96, 214, BAR_W, BAR_H);
    };

    panel(0, g_chars[playerIdx].face, false,
          player.hp(), player.maxHp(), player.f.mp, player.f.maxMp);
    for (int i = 0; i < NUM_ENEMIES; i++)
        panel(i + 1, g_chars[slots[i].rosterIdx].face, true,
              slots[i].e.a.hp(), slots[i].e.a.maxHp(),
              slots[i].e.a.f.mp, slots[i].e.a.f.maxMp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Menu: character select
// ─────────────────────────────────────────────────────────────────────────────
static void renderMenu(SDL_Renderer* r, int cursor) {
    fillRect(r, 0, 0, SCREEN_W, SCREEN_H, 10, 10, 40);
    // Title strip
    fillRect(r, SCREEN_W/2 - 260, 40, 520, 8, 220, 170, 0);
    // 8 portrait cards, 4×2 grid.
    constexpr int CARD = 132, GAP = 24;
    int gridW = 4 * CARD + 3 * GAP;
    int x0 = (SCREEN_W - gridW) / 2, y0 = 130;
    for (int i = 0; i < ROSTER_N; i++) {
        int cx = x0 + (i % 4) * (CARD + GAP);
        int cy = y0 + (i / 4) * (CARD + GAP + 16);
        bool sel = (i == cursor);
        if (sel) fillRect(r, cx - 6, cy - 6, CARD + 12, CARD + 12, 240, 200, 40);
        fillRect(r, cx - 2, cy - 2, CARD + 4, CARD + 4, 18, 36, 90);
        if (g_chars[i].face) {
            SDL_Rect d = { cx, cy, CARD, CARD };
            SDL_RenderCopy(r, g_chars[i].face, nullptr, &d);
        }
    }
    // Blink "press attack"
    if ((SDL_GetTicks() / 500) % 2 == 0)
        fillRect(r, SCREEN_W/2 - 120, SCREEN_H - 60, 240, 16, 255, 255, 255);
}

static void renderGameOver(SDL_Renderer* r, bool playerWon, int timer) {
    fillRect(r, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 160);
    if (playerWon) fillRect(r, SCREEN_W/2 - 160, 220, 320, 60,  30, 180,  30);
    else           fillRect(r, SCREEN_W/2 - 160, 220, 320, 60, 180,  30,  30);
    int pct = timer * SCREEN_W / 180;
    fillRect(r, 0, SCREEN_H - 8, pct, 8, 255, 255, 255);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game reset
// ─────────────────────────────────────────────────────────────────────────────
static void resetGame(SDL_Renderer* r, lf2::Player& player, int playerIdx,
                      EnemySlot slots[], GameSt& gameSt)
{
    g_chars[playerIdx].loadTextures(r, ROSTER[playerIdx]);
    // Warm every projectile/weapon this fighter can emit BEFORE the match starts,
    // so firing a special never blocks the loop on a .dat + spritesheet load.
    preloadObjAssets(r, g_chars[playerIdx].data);
    objAssets(r, 999);                                 // broken-weapon debris
    player = lf2::Player();
    player.load(&g_chars[playerIdx].data);
    player.x = 400.f;
    player.z = (float)Z_MIN;
    player.right = true;

    const float ex[] = { 1600.f, 2200.f, 2800.f };
    const float ez[] = { (float)Z_MIN, (float)(Z_MIN + Z_MAX) / 2, (float)Z_MAX };
    const float off[] = { -50.f, 50.f, -50.f };
    for (int i = 0; i < NUM_ENEMIES; i++) {
        int idx = (playerIdx + 1 + i) % ROSTER_N;      // 3 different opponents
        g_chars[idx].loadTextures(r, ROSTER[idx]);
        preloadObjAssets(r, g_chars[idx].data);        // enemies fire specials too
        slots[i] = EnemySlot();
        slots[i].rosterIdx = idx;
        slots[i].e.load(&g_chars[idx].data);
        slots[i].e.a.x = ex[i];
        slots[i].e.a.z = ez[i];
        slots[i].e.aimOffset = off[i];
        slots[i].e.frozen = true;   // TEST MODE: born idle; Start enables the AI
    }
    g_objects.clear();

    // Test weapons on the ground (knife 120 = light, stone 150 = heavy/solid).
    // Stand next to one and press Attack to pick it up.
    const int woids[] = { 120, 150 };
    const float wxs[]  = { 700.f, 1000.f };
    for (int i = 0; i < 2; i++) {
        ObjAssets* oa = objAssets(r, woids[i]);
        if (!oa) continue;
        lf2::Object* o = g_objects.alloc();
        if (!o) continue;
        o->spawn(&oa->data, objBankIndex(oa), wxs[i], (float)Z_MIN, (float)Z_MIN,
                 true, lf2::weapon_frame::ON_GROUND, /*team=*/0);
        const dat::ObjectEntry* e = g_index.object(woids[i]);
        o->weaponType = e ? e->type : 1;
        o->restOnGround(wxs[i], (float)Z_MIN, (float)Z_MIN, true);
    }
    gameSt = GameSt::PLAYING;
}

// Current-frame wpoint. Holder frames use kind 1 (hold) / 3 (throw); a held
// weapon's own frames use kind 2. Returns the kind found (0 = none).
static int fighterWpoint(const lf2::Fighter& f, dat::Wpoint& out) {
    const dat::Frame* fr = f.cur();
    if (!fr) return 0;
    for (const auto& w : fr->wpoints)
        if (w.kind == 1 || w.kind == 2 || w.kind == 3) { out = w; return w.kind; }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────
int main(int, char*[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    IMG_Init(IMG_INIT_PNG);

    g_index = dat::loadIndex("app0:/data/data.txt");
    for (int i = 0; i < ROSTER_N; i++)
        g_chars[i].data = dat::load((std::string("app0:/data/") + ROSTER[i] + ".dat").c_str());

    SDL_Joystick* joy = nullptr;
    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

    SDL_Window*   win = SDL_CreateWindow("LF2 Vita v0.8.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Scene scene;
    scene.load(ren, "app0:/bg/sys/lf/bg.dat");   // Lion Forest (only stage wired so far)
    // Faces up-front (small): the select screen needs all of them.
    for (int i = 0; i < ROSTER_N; i++)
        g_chars[i].face = loadTex(ren,
            (std::string("app0:/assets/") + ROSTER[i] + "_f.png").c_str(), false);

    lf2::Player player;
    EnemySlot   slots[NUM_ENEMIES];
    GameSt gameSt        = GameSt::MENU;
    int    menuCursor    = 0;
    int    playerIdx     = 0;
    int    gameOverTimer = 0;
    bool   playerWon     = false;
    int    lastSwingId   = -1;
    int    playerPrevFrame = -1;

    bool prevStart = false;
    bool aiEnabled = false;   // TEST MODE default: enemies frozen until Start
    bool prevAtk = false, prevJmp = false, prevL = false, prevR = false,
         prevU = false, prevD = false;

    Uint32 nextTick = SDL_GetTicks();
    bool   running  = true;
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT) running = false;

        Uint32 now = SDL_GetTicks();
        while (now >= nextTick) {
            InputState raw = readInput(joy);
            bool atk  = raw.atk && !prevAtk;
            bool jmp  = raw.jmp && !prevJmp;
            bool newL = raw.L && !prevL, newR = raw.R && !prevR;
            bool newU = raw.U && !prevU, newD = raw.D && !prevD;
            bool newStart = raw.start && !prevStart;
            prevAtk = raw.atk; prevJmp = raw.jmp; prevStart = raw.start;
            prevL = raw.L; prevR = raw.R; prevU = raw.U; prevD = raw.D;

            // TEST MODE: enemies spawn frozen (they still react to hits); Start
            // toggles their AI so specials can be tested on stationary targets.
            if (newStart) {
                aiEnabled = !aiEnabled;
                for (int i = 0; i < NUM_ENEMIES; i++) slots[i].e.frozen = !aiEnabled;
            }

            if (gameSt == GameSt::MENU) {
                if (newR) menuCursor = (menuCursor + 1) % ROSTER_N;
                if (newL) menuCursor = (menuCursor + ROSTER_N - 1) % ROSTER_N;
                if (newD) menuCursor = (menuCursor + 4) % ROSTER_N;
                if (newU) menuCursor = (menuCursor + ROSTER_N - 4) % ROSTER_N;
                if (atk || jmp) {
                    playerIdx = menuCursor;
                    resetGame(ren, player, playerIdx, slots, gameSt);
                    lastSwingId = -1;
                    playerPrevFrame = -1;
                    aiEnabled = false;   // every match starts with frozen enemies
                }
            }
            else if (gameSt == GameSt::PLAYING) {
                float prevX = player.x, prevZ = player.z;   // for the itr-14 block
                int   prevState = player.f.state();         // pre-tick: run vs pickup
                player.tick(raw.L, raw.R, raw.U, raw.D, atk, jmp, raw.def, raw.spc);
                for (int i = 0; i < NUM_ENEMIES; i++)
                    slots[i].e.tick(player.x, player.z);

                // ── opoint spawns (on frame entry) ────────────────────────────
                if (player.f.frameId != playerPrevFrame) {
                    spawnOpoints(ren, player.f, 0);
                    playerPrevFrame = player.f.frameId;
                }
                for (int i = 0; i < NUM_ENEMIES; i++) {
                    if (slots[i].e.a.f.frameId != slots[i].prevFrame) {
                        spawnOpoints(ren, slots[i].e.a.f, 1);
                        slots[i].prevFrame = slots[i].e.a.f.frameId;
                    }
                }

                // ── objects fly ───────────────────────────────────────────────
                g_objects.forEach([&](lf2::Object& o) { o.tick(); });

                // ── weapons: pick up / hold / drop ────────────────────────────
                // Pick up a ground weapon by walking over it while the current
                // frame offers a hold point (wpoint kind 1).
                // Pick up: press ATTACK next to a grounded weapon (LF2 picks up
                // on the attack input, not by walking over it).
                // Running/dashing into an object is a RUNNING ATTACK, not a
                // pickup — the run branch already picked its attack frame, so
                // grabbing here would cancel it.
                // Tested on the PRE-tick state: by now the run branch has already
                // swapped in the running-attack frame (state 3).
                bool busyRunning = (prevState == lf2::ST_RUNNING || prevState == lf2::ST_DASH);
                if (player.heldWeapon < 0 && player.alive() && atk &&
                    player.grounded() && !busyRunning) {
                    for (int i = 0; i < g_objects.SIZE; i++) {
                        lf2::Object& o = g_objects.objs[i];
                        if (o.active && o.weaponType > 0 && !o.held && !o.thrown &&
                            fabsf(o.f.x - player.x) < 60.f &&
                            fabsf(o.f.z - player.z) < 20.f) {
                            o.held = true;
                            player.heldWeapon  = i;
                            player.heavyWeapon = (o.weaponType >= 2);
                            // F.LF: picking up plays 115 (light) / 116 (heavy)
                            player.f.setFrame(player.heavyWeapon ? lf2::fid::PICK_HEAVY
                                                                 : lf2::fid::PICK_LIGHT);
                            break;
                        }
                    }
                }
                // Position the held weapon at the holder's wpoint each tick; a
                // throw frame (wpoint kind 3) or a knockdown drops it.
                if (player.heldWeapon >= 0) {
                    lf2::Object& w = g_objects.objs[player.heldWeapon];
                    dat::Wpoint wp; int wk = fighterWpoint(player.f, wp);
                    int pst = player.f.state();
                    // THROW: LF2 signals the release with the HOLD wpoint (kind 1)
                    // carrying a velocity — there is NO kind-3 in the character data
                    // (dennis throw frames 47/51/54 are kind 1 with dvx). So a hold
                    // wpoint with nonzero dv releases the weapon this tick. (The
                    // old wk==3 gate never matched, so throwing was impossible.)
                    bool throwIt = (wk == 1 || wk == 3) && (wp.dvx || wp.dvy || wp.dvz);
                    // wk == 0: the current frame has NO hold point (jump_attack,
                    // run_attack, injured…). There's nowhere to put the weapon, so
                    // it must fall — leaving it held froze it in mid-air (tick()
                    // early-returns while held), which read as "the weapon floats".
                    bool dropIt  = !w.active || !player.alive() || wk == 0 ||
                                   pst == lf2::ST_FALLING || pst == lf2::ST_LYING ||
                                   pst == lf2::ST_INJURED;
                    if (throwIt && w.active) {
                        w.groundY = player.z;      // lands back on the player's floor line
                        w.throwFrom(player.x, player.z, player.z, player.right,
                                    (float)wp.dvx, (float)wp.dvy, (float)wp.dvz);
                        player.heldWeapon = -1; player.heavyWeapon = false;
                    } else if (dropIt) {
                        // Drop it slightly IN FRONT (beyond the solid radius) so a
                        // heavy weapon doesn't rest under the holder and shove them.
                        if (w.active) {
                            float dropX = player.x + (player.right ? 40.f : -40.f);
                            // Drop from the holder's CURRENT height (z+h when in the
                            // air) and let gravity land it on the floor line.
                            w.dropAt(dropX, player.z + player.h, player.z,
                                     player.right, player.z);
                        }
                        player.heldWeapon = -1; player.heavyWeapon = false;
                    } else if (wk == 1) {
                        // F.LF weapon.act(): the weapon's OWN wpoint is made to
                        // COINCIDE with the holder's wpoint — not its center. Set
                        // the weaponact frame first, then offset the weapon so its
                        // own wpoint lands on the holder's hand.
                        w.f.facingRight = player.right;
                        if (w.f.data && w.f.data->frame(wp.weaponact)) w.f.setFrame(wp.weaponact);
                        float hx, hy; player.f.pointWorld(wp.x, wp.y, hx, hy);
                        w.f.x = hx; w.f.y = hy; w.f.z = player.z;
                        dat::Wpoint own;
                        if (fighterWpoint(w.f, own)) {     // shift so own wpoint == hand
                            float ox, oy; w.f.pointWorld(own.x, own.y, ox, oy);
                            w.f.x += hx - ox; w.f.y += hy - oy;
                        }
                    }
                }

                // Obstacles are DATA-DRIVEN, via itr kind 14: weapon1.dat (stone)
                // carries one on its on_ground frame 20, weapon4.dat (knife) none —
                // so only big objects at rest block, and only over that 16×18 box.
                // F.LF (mechanics.blocking_xz) scales the blocked fighter's motion
                // to 10% instead of displacing it; the old "solid" code snapped
                // player.x by ±34 px per tick, which is what shoved the player when
                // picking a stone up or throwing it.
                // A big object STOPS you (F.LF only slows to 10%, but at 10% you
                // still creep through the stone, which reads as walking inside it).
                if (blockedByObstacle(player, g_objects, player.heldWeapon)) {
                    player.x = prevX;
                    player.z = prevZ;
                    player.clampPos();
                    player.syncAnchor();
                }

                // ── new player swing re-arms the enemies ─────────────────────
                if (player.swingId != lastSwingId) {
                    for (int i = 0; i < NUM_ENEMIES; i++) slots[i].e.rehitTimer = 0;
                    lastSwingId = player.swingId;
                }

                // ── player attack → enemies ──────────────────────────────────
                HitInfo hi;
                if (fighterAttack(player.f, hi)) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        Box ebody;
                        Enemy& e = slots[i].e;
                        int es = e.a.f.state();
                        // OpenLF2 anti-juggle: a light hit (fall<=40) can't strike
                        // a victim already falling; a launcher (fall>40) still can.
                        bool downed = (es == lf2::ST_FALLING && hi.fall <= 40) ||
                                      es == lf2::ST_LYING;
                        if (e.rehitTimer == 0 && e.alive() && !downed &&
                            fabsf(player.z - e.a.z) < (float)hi.zwidth &&
                            fighterBody(e.a.f, ebody) && boxOverlap(hi.box, ebody))
                        {
                            e.rehitTimer = hi.rest;
                            e.hitFlash   = 10;
                            float kb = (float)(hi.dvx > 0 ? hi.dvx : 1);
                            e.a.hit(hi.injury, player.right ? kb : -kb, hi.fall);
                        }
                    }
                    // …and the same swing damages WEAPONS lying around: they have
                    // <weapon_hp> durability (stone 800, knife 200) and break at 0.
                    damageObjects(ren, hi, player.right, player.heldWeapon);
                }

                // ── held weapon's itr → enemies ──────────────────────────────
                // F.LF: a held weapon strikes through its itr KIND 5, and the real
                // damage comes from <weapon_strength_list>[wpoint.attacking]
                // (1 normal · 2 jump · 3 run · 4 dash), not from the itr itself.
                if (player.heldWeapon >= 0) {
                    lf2::Object& w = g_objects.objs[player.heldWeapon];
                    dat::Wpoint hw; fighterWpoint(player.f, hw);
                    HitInfo whi; bool swinging = false;
                    if (w.active && hw.attacking > 0) {
                        w.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
                            if (it.kind != 5 || swinging) return;
                            whi.box = toBox(wb);
                            whi.injury = it.injury; whi.fall = it.fall;
                            whi.dvx = it.dvx;
                            whi.rest = it.vrest > 0 ? it.vrest : 9;   // weapon default
                            whi.zwidth = it.zwidth > 0 ? it.zwidth : 15;
                            swinging = true;
                        });
                        const dat::File* wd = w.f.data;
                        if (swinging && wd && hw.attacking < 8 && wd->strength[hw.attacking].valid) {
                            const dat::StrengthEntry& se = wd->strength[hw.attacking];
                            whi.injury = se.injury; whi.fall = se.fall; whi.dvx = se.dvx;
                            if (se.vrest > 0) whi.rest = se.vrest;
                        }
                    }
                    if (swinging) {
                        damageObjects(ren, whi, player.right, player.heldWeapon);
                        for (int i = 0; i < NUM_ENEMIES; i++) {
                            Box ebody; Enemy& e = slots[i].e;
                            if (e.rehitTimer == 0 && e.alive() &&
                                fabsf(player.z - e.a.z) < (float)whi.zwidth &&
                                fighterBody(e.a.f, ebody) && boxOverlap(whi.box, ebody)) {
                                e.rehitTimer = whi.rest;
                                e.hitFlash   = 10;
                                float kb = (float)(whi.dvx > 0 ? whi.dvx : 4);
                                e.a.hit(whi.injury, player.right ? kb : -kb, whi.fall);
                            }
                        }
                    }
                }

                // ── enemy attacks → player ───────────────────────────────────
                Box pBody;
                bool havePBody = player.alive() && fighterBody(player.f, pBody);
                if (havePBody) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        HitInfo ehi;
                        Enemy& e = slots[i].e;
                        if (!e.hasHitPlayer && e.alive() &&
                            fabsf(player.z - e.a.z) < (float)ehi.zwidth &&
                            fighterAttack(e.a.f, ehi) && boxOverlap(ehi.box, pBody))
                        {
                            e.hasHitPlayer = true;
                            float kb = (float)(ehi.dvx > 0 ? ehi.dvx : 1);
                            player.hit(ehi.injury, e.a.right ? kb : -kb, ehi.fall);
                        }
                    }
                }

                // ── projectiles hit actors ───────────────────────────────────
                g_objects.forEach([&](lf2::Object& o) {
                    // Projectiles hit while in their flying state; a THROWN weapon
                    // hits with its own itr while airborne.
                    if (!(o.flying() || o.thrown) || o.rehit > 0) return;
                    HitInfo ohi;
                    if (!fighterAttack(o.f, ohi)) return;
                    if (o.team == 0) {                       // player's ball → enemies
                        for (int i = 0; i < NUM_ENEMIES; i++) {
                            Box ebody;
                            Enemy& e = slots[i].e;
                            if (e.alive() && fabsf(o.f.z - e.a.z) < (float)ohi.zwidth &&
                                fighterBody(e.a.f, ebody) && boxOverlap(ohi.box, ebody))
                            {
                                float kb = (float)(ohi.dvx > 0 ? ohi.dvx : 4);
                                e.a.hit(ohi.injury, o.f.facingRight ? kb : -kb, ohi.fall);
                                e.hitFlash = 10;
                                o.onHit();
                                o.rehit = ohi.rest;
                                break;
                            }
                        }
                    } else if (havePBody) {                  // enemy ball → player
                        if (fabsf(o.f.z - player.z) < (float)ohi.zwidth && boxOverlap(ohi.box, pBody)) {
                            float kb = (float)(ohi.dvx > 0 ? ohi.dvx : 4);
                            player.hit(ohi.injury, o.f.facingRight ? kb : -kb, ohi.fall);
                            o.onHit();
                            o.rehit = ohi.rest;
                        }
                    }
                });

                bool allDead = true;
                for (int i = 0; i < NUM_ENEMIES; i++)
                    if (slots[i].e.alive()) { allDead = false; break; }
                if (!player.alive() || allDead) {
                    playerWon     = allDead;
                    gameSt        = GameSt::GAMEOVER;
                    gameOverTimer = 180;
                }
            }
            else { // GAMEOVER
                if (--gameOverTimer <= 0) gameSt = GameSt::MENU;
            }

            nextTick += TICK_MS;
        }

        if (gameSt == GameSt::MENU) {
            renderMenu(ren, menuCursor);
        } else {
            int camX = clampI((int)player.x - SCREEN_W / 2, 0, MAP_W - SCREEN_W);
            renderBackground(ren, scene, camX);

            // Shadows — size from bg.dat's shadowsize (Lion Forest: 37×9).
            if (scene.shadow) {
                const int shW = scene.bg.shadowW, shH = scene.bg.shadowH;
                const int shOx = shW / 2;
                SDL_Rect ps = { (int)player.x - camX - shOx, (int)player.z - shH/2, shW, shH };
                SDL_RenderCopy(ren, scene.shadow, nullptr, &ps);
                for (int i = 0; i < NUM_ENEMIES; i++) {
                    SDL_Rect es = { (int)slots[i].e.a.x - camX - shOx,
                                    (int)slots[i].e.a.z - shH/2, shW, shH };
                    SDL_RenderCopy(ren, scene.shadow, nullptr, &es);
                }
                g_objects.forEach([&](lf2::Object& o) {
                    SDL_Rect os = { (int)o.f.x - camX - shOx, (int)o.f.z - shH/2, shW, shH };
                    SDL_RenderCopy(ren, scene.shadow, nullptr, &os);
                });
            }

            // Actors AND objects, painter's order by z — weapons/projectiles must
            // sort with the fighters, not blit on top of everyone.
            struct DrawRef { float z; int kind; int idx; };   // 0 player, 1 enemy, 2 object
            DrawRef list[NUM_ENEMIES + 1 + lf2::ObjectPool<24>::SIZE];
            int nDraw = 0;
            list[nDraw++] = { player.z, 0, 0 };
            for (int i = 0; i < NUM_ENEMIES; i++) list[nDraw++] = { slots[i].e.a.z, 1, i };
            for (int i = 0; i < g_objects.SIZE; i++)
                if (g_objects.objs[i].active) list[nDraw++] = { g_objects.objs[i].f.z, 2, i };
            for (int i = 1; i < nDraw; i++) {
                DrawRef key = list[i]; int j = i - 1;
                while (j >= 0 && list[j].z > key.z) { list[j + 1] = list[j]; j--; }
                list[j + 1] = key;
            }
            for (int i = 0; i < nDraw; i++) {
                if (list[i].kind == 0) {
                    drawFighter(ren, player.f, g_chars[playerIdx].sheets, camX);
                } else if (list[i].kind == 1) {
                    EnemySlot& s = slots[list[i].idx];
                    bool flash = s.e.hitFlash > 0;
                    drawFighter(ren, s.e.a.f, g_chars[s.rosterIdx].sheets, camX,
                                255, flash ? 80 : 255, flash ? 80 : 255);
                } else {
                    lf2::Object& o = g_objects.objs[list[i].idx];
                    if (o.sheetSlot >= 0 && o.sheetSlot < (int)g_objBank.size())
                        drawFighter(ren, o.f, g_objBank[o.sheetSlot].sheets, camX);
                }
            }

            renderHUD(ren, player, playerIdx, slots);
            if (gameSt == GameSt::GAMEOVER)
                renderGameOver(ren, playerWon, gameOverTimer);
        }

        SDL_RenderPresent(ren);
    }

    if (joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    sceKernelExitProcess(0);
    return 0;
}
