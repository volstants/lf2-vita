#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#ifndef LF2_HOST
#include <psp2/kernel/processmgr.h>
#endif
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
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

// Where packaged files live. On the Vita everything is under app0:/; a host
// build (LF2_HOST) runs straight out of the repo, which is what lets the whole
// game loop be exercised headless with SDL_VIDEODRIVER=dummy.
// Log de diagnostico. Na Vita vai para ux0:data/, que sobrevive ao fechamento
// do app e e' acessivel por FTP/USB sem reinstalar o VPK.
#ifdef LF2_HOST
static const char* LOG_PATH = "lf2vita.log";
#else
static const char* LOG_PATH = "ux0:data/lf2vita.log";
#endif

#ifdef LF2_HOST
static const std::string APP = "";
#else
static const std::string APP = "app0:/";
#endif

// "sprite\sys\dennis_0.bmp" → "<APP>assets/dennis_0.png"
static std::string sheetAsset(const std::string& datPath) {
    size_t slash = datPath.find_last_of("\\/");
    std::string base = (slash == std::string::npos) ? datPath : datPath.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return APP + "assets/" + base + ".png";
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
        face = loadTex(r, (APP + "assets/" + name + "_f.png").c_str(), false);
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
        oa.data = dat::load((APP + path).c_str());
        if (!oa.data.frames.empty())
            for (const auto& s : oa.data.header.files)
                oa.sheets.push_back(loadTex(r, sheetAsset(s.path).c_str(), true, 0, 0, 0));
    }
    g_objBank.push_back(std::move(oa));               // cache hits AND misses
    ObjAssets* back = &g_objBank.back();
    return back->data.frames.empty() ? nullptr : back;
}

// Cache-only lookup: no renderer, no disk. This is what the tick loop uses, so
// simulation never needs a live SDL_Renderer (and never stalls on I/O). Anything
// it could need was warmed by preloadObjAssets() at match start.
static ObjAssets* objAssetsCached(int oid) {
    for (auto& o : g_objBank)
        if (o.oid == oid) return o.data.frames.empty() ? nullptr : &o;
    return nullptr;
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
    int injury = 20, fall = -1, dvx = 0, zwidth = 15;
    // `arest` e `vrest` NÃO são o mesmo temporizador e o engine os guarda em
    // lugares de naturezas diferentes. Ver applyRest() abaixo.
    int bdefend = 0;       // itr+0x40 — acumula na vitima; > 30 quebra a guarda
    int arest = 0;         // itr+0x20 — cooldown ESCALAR do atacante
    int vrest = 0;         // itr+0x24 — cooldown POR PAR (atacante, vítima)
    int kind = 0;          // itr kind that produced this hit (see itrDeals)
    int effect = 0;        // itr `effect`: fire / ice / bleed (see Player::hit)
    int attState = 0;      // the ATTACKER's frame state — the effect gate below
                           // needs it (a burn_run attacker is treated apart).
    // OpenLF2 class_global.c:204-230: an itr with vrest == 0 is SINGLE TARGET —
    // the original compares abs(attacker->x - injured->x) across candidates and
    // keeps only the closest one (attackable_distance / successful_attacks = 1).
    // Only vrest > 0 accumulates several victims in one tick. We used to damage
    // every overlapping enemy, so one punch could hit a whole stack.
    bool singleTarget = true;
};
// Which itr kinds actually deal damage. OpenLF2 (class_global.c:268) says every
// kind EXCEPT 1 (catch_injured), 2 (pick_up_weapon) and 7 (rowing_pick) counts as
// a successful attack — but "successful" only means the hit registers; the damage
// payload still comes from the itr, and a census of the 67 .dat files shows which
// kinds carry one:
//    0  normal_attack   876 with injury   → damage (was the only one we accepted)
//    4  thrown          286 with injury   → gated on the attacker's thrown_injury
//                                           (OpenLF2:255), which only a cpoint
//                                           throw sets — inert until throws exist
//    5  strength_list   235 with injury   → weapons, handled separately
//    6  super_punch       0 with injury   → NOT an attack: it marks vulnerable
//                                           frames (broken_defend 112-114,
//                                           injured 226-229). Ignoring it is right.
//    8  heal              5               → john_ball; heals, must not damage
//    9  forcefield        6               → john_ball; own effect
//   10  flute            15 with injury   → Henry, IN OUR ROSTER
//   11  float             5 with injury   → Henry, IN OUR ROSTER
//   15  fly               5 with injury   → freeze_column (Freeze is in the roster)
//   16  freeze            1 with injury   → freeze_column
//    1/2/3/7/14           0 with injury   → catch / pickup / stop, never damage
static inline bool itrDeals(int kind) {
    return kind == 0 || kind == 10 || kind == 11 || kind == 15 || kind == 16;
}
// OpenLF2 class_global.c:178-181 — the anti-juggle rule spares two kinds: flute
// and float still connect with a victim who is already falling.
static inline bool itrIgnoresAntiJuggle(int kind) {
    return kind == 10 || kind == 11;
}

// Grava os dois temporizadores de re-acerto de um golpe que conectou.
//
// SOURCE: lf2.exe 0x0042f2c8-0x0042f31b (com o gêmeo em 0x004303a7-0x00430410).
// O trecho, com `ecx` = itr, `ebx` = id do atacante, `edi` = id da vítima:
//
//     42f2d6:  mov  0x20(%ecx),%eax      ; itr->arest
//     42f2d9:  cmp  $0x4,%eax
//     42f2dc:  jge  0x42f2f7             ; arest >= 4 → usa o valor do itr
//     42f2de:  cmpl $0x0,0x24(%ecx)      ; itr->vrest
//     42f2e2:  jne  0x42f2f7
//     42f2e4:  movl $0x4,0xec(%eax)      ; senão: atacante->arest = 4  (PISO)
//     42f2f7:  mov  %eax,0xec(%edx)      ; atacante->arest = itr->arest
//     42f304:  cmpl $0x0,0x24(%ecx)
//     42f308:  jle  0x42f31b             ; vrest <= 0 → não grava nada por par
//     42f314:  mov  %cl,0xf0(%eax,%ebx,1); vitima->vrest_of_objects[atacante]
//
// Três fatos que estavam errados aqui: (1) `arest` é escrito SEMPRE, mesmo
// quando há `vrest`; (2) o piso é 4 e só se aplica quando `arest < 4` E
// `vrest == 0` — o 8/9 que usávamos era invenção; (3) o `vrest` é gravado como
// BYTE no array da VÍTIMA indexado pelo ATACANTE, não no atacante.
// EM ABERTO — teto de 12: o sítio 2 satura o arest logo após gravá-lo
// (`0x4303bb: mov $0xc,%edx` · `0x4303c0: cmp` · `0x4303c8: mov %edx,0xec(%eax)`).
// O sítio 1 NÃO satura: de `0x0042f2fe` vai direto ao vrest em `0x0042f304`.
// Os dois compartilham o mesmo piso de 4, então são caminhos irmãos, mas ainda
// não isolei a condição que separa um do outro. 143 itrs do jogo usam arest, 51
// deles com valor 15 — aplicar o teto sem saber a que ramo ele pertence mudaria
// o comportamento de metade dos golpes com base em meia evidência. Fica de fora
// até a condição estar reconstruída.
static inline void applyRest(const HitInfo& hi, int& attackerArest, int& pairVrest) {
    lf2::applyRest(hi.arest, hi.vrest, attackerArest, pairVrest);
}

static bool fighterAttack(const lf2::Fighter& f, HitInfo& out) {
    bool found = false;
    out.attState = f.state();
    f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
        if (itrDeals(it.kind) && !found) {
            out.kind = it.kind;
            out.box    = toBox(wb);
            out.injury = it.injury > 0 ? it.injury : 20;
            out.fall   = it.fall;
            out.dvx    = it.dvx;
            out.bdefend = it.bdefend;
            out.arest  = it.arest;
            out.vrest  = it.vrest;
            out.singleTarget = (it.vrest == 0);
            out.effect = it.effect;
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
static void damageObjects(const HitInfo& hi, bool fromRight, int skipIdx,
                          bool attackerArmed, int& attackerArest);

// Obstacle test: does any grounded object present an itr of kind 14 (LF2's
// "blocking" box) where the player's body now is? Only weapon1.dat (stone) ships
// one, on its resting frame — knives and airborne/held weapons never block.
// The test is x/y + a z band, so jumping clears it for free: in the air the
// player's body box no longer overlaps the obstacle's ground-level box.
template <int N>
static bool blockedByObstacle(const lf2::Player& p, lf2::ObjectPool<N>& objs, int heldIdx,
                              float* obstacleX = nullptr, float* obstacleZ = nullptr) {
    Box pb;
    if (!fighterBody(p.f, pb)) return false;
    bool blocked = false;
    for (int i = 0; i < objs.SIZE; i++) {
        if (i == heldIdx) continue;
        lf2::Object& o = objs.objs[i];
        // Only a held weapon is exempt. Airborne ones need no special case: the
        // kind-14 box lives on the on_ground frame, so a stone in flight has none.
        // Skipping `thrown` objects meant a stone that was merely knocked into a
        // hop stopped blocking, and the player walked straight through it.
        if (!o.active || o.held) continue;
        o.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
            if (it.kind != 14 || blocked) return;
            float zw = (float)(it.zwidth > 0 ? it.zwidth : 15);
            if (fabsf(o.f.z - p.z) >= zw) return;
            if (boxOverlap(toBox(wb), pb)) {
                blocked = true;
                if (obstacleX) *obstacleX = o.f.x;
                if (obstacleZ) *obstacleZ = o.f.z;
            }
        });
        if (blocked) break;
    }
    return blocked;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
struct InputState { bool L, R, U, D, atk, jmp, def, spc, start, sel, any; };

#ifdef LF2_HEADLESS
// ── Scripted input for the headless harness ──────────────────────────────────
// No joystick exists under SDL_VIDEODRIVER=dummy, so the harness drives the
// same tick loop the device runs. The script is deliberately dumb: it mashes
// attack/special while walking, which is exactly the pattern that exposed the
// object-pool exhaustion and the frozen-slot bugs on device.
static long g_tickNo = 0;
static long g_headlessTicks = 30 * 60;   // 60 s of simulated play by default
static int  g_peakObjects   = 0;         // high-water mark of the object pool
static long g_poolFullTicks = 0;         // ticks spent with every slot taken
static long g_playTicks     = 0;         // ticks actually inside a match
static int  g_headlessChar  = 0;         // roster index the harness plays
static long g_spawnDrops    = 0;         // opoints that found the pool full
static long g_spawnTotal    = 0;         // opoints that produced an object
static long g_resets        = 0;         // matches restarted (clears the pool)
static long g_damageDealt   = 0;         // total HP removed from the enemies
static InputState scriptedInput() {
    InputState in{};
    long t = g_tickNo;
    if (t < 4)          in.start = true;              // pick the fighter, start
    else if (t == 6)     in.sel   = true;              // AUDIT MODE on
    else {
        long c = t % 60;
        in.R   = c < 20;                              // walk in, walk out
        in.L   = c >= 40;
        // Hold Square a few ticks: a 1-tick tap lands mid-punch and is eaten,
        // so the special (and its opoint) would never fire.
        in.spc = (c >= 20 && c < 24) || (c >= 44 && c < 48);
        in.atk = (c == 30 || c == 34);
        in.jmp = (c == 50);
    }
    in.any = in.atk || in.jmp || in.def || in.spc || in.L || in.R || in.U || in.D;
    return in;
}
#endif

static InputState readInput(SDL_Joystick* joy) {
#ifdef LF2_HEADLESS
    (void)joy;
    return scriptedInput();
#endif
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
    in.sel   = SDL_JoystickGetButton(joy, BTN_SELECT);
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
// 48, not 24: Rudolf throws 4 shuriken per swing and Henry can keep several
// arrows in the air plus the ones still lying on the ground, and a full pool
// silently drops new spawns (the attack animates with nothing coming out).
static lf2::ObjectPool<48> g_objects;

struct EnemySlot { Enemy e; int rosterIdx = 3; int prevFrame = -1; };

// Spawn every kind-1 opoint of the frame the fighter JUST entered.
static void spawnOpoints(const lf2::Fighter& f, int team) {
    const dat::Frame* fr = f.cur();
    if (!fr || fr->opoints.empty()) return;
    for (const dat::Opoint& op : fr->opoints) {
        if (op.kind != 1) continue;
        // oid 5 is NOT an object. F.LF character.js:1969-1998 special-cases it
        // BEFORE the create_object/create_multiple_objects split: it spawns
        // `number_of_character = |facing| / 10` AI-controlled +man clones (20 HP,
        // AIscript 4, parented to the caster), which is Rudolf's frame 271
        // transform. data/data.txt:19 confirms `id: 5 type: 0 file:
        // data\rudolf.dat` — a CHARACTER file, so routing it through the object
        // pool builds a Fighter out of a character .dat and files it as a
        // projectile. The multi-spawn work then made it TWO of them.
        // We have no NPC spawning yet, so emit nothing rather than something
        // wrong; this is the hook to fill in when roster spawning lands.
        if (op.oid == 5) continue;
        ObjAssets* oa = objAssetsCached(op.oid);
        if (!oa) continue;
        // `facing` is NOT just a direction: it encodes COUNT * 10 + direction.
        // F.LF character.js:2002 — `if (Math.abs(facing) > 10)
        // create_multiple_objects(op, parent, floor(facing/10), dvz || 3)`.
        // henry frame 286 is literally named 5_arrow with facing 50, rudolf 288
        // throws 5 shuriken and his frame 271 (+man) spawns 2 clones with facing
        // 20. We were reading facing purely as a direction and emitting ONE.
        int count = (abs(op.facing) > 10) ? (abs(op.facing) / 10) : 1;
        int dir   = abs(op.facing) % 10;
        bool faceRight = (dir == 1) ? !f.facingRight : f.facingRight;
        // F.LF passes `ops[i].dvz || 3`; the census of the 115 opoints across the
        // 67 .dat files shows the sub-keys are exactly kind/x/y/action/dvx/dvy/
        // oid/facing — there is no dvz anywhere, so the fallback always wins.
        const float spread = 3.f;
        const int   half   = count / 2;
        for (int n = 0; n < count; ++n) {
            // vz_array, ported from F.LF match.js:316-353. The copies fan out by
            // DEPTH VELOCITY and all leave the same point; a static z offset (what
            // this used to do) puts them side by side at birth and then keeps them
            // exactly parallel forever, so they never actually spread.
            // t runs [-half, half], skipping 0 when `count` is even so the list
            // still holds exactly `count` entries: 5 → -2..2, 4 → -2,-1,1,2.
            int t = -half + n;
            if ((count % 2) == 0 && t >= 0) ++t;      // hop over the centre lane
            const float vzi = (count > 1) ? (float)t * spread : 0.f;
            lf2::Object* o = g_objects.alloc();
            if (!o) {
#ifdef LF2_HEADLESS
                ++g_spawnDrops;
#endif
                continue;
            }
#ifdef LF2_HEADLESS
            ++g_spawnTotal;
#endif
            float wx, wy;
            f.pointWorld(op.x, op.y, wx, wy);
            float vx0 = faceRight ? (float)op.dvx : -(float)op.dvx;
            // Each copy also gives up |vz| of forward speed (F.LF: vx -= |vz|
            // facing right, vx += |vz| facing left). That is what turns the fan
            // into a spread — the outer arrows lag as they drift apart.
            if (faceRight) vx0 -= fabsf(vzi); else vx0 += fabsf(vzi);
            o->spawn(&oa->data, objBankIndex(oa), wx, wy, f.z,
                     faceRight, op.action, team, vx0, (float)op.dvy);
            o->vz = vzi;
            const dat::ObjectEntry* oe = g_index.object(op.oid);
            int oty = oe ? oe->type : 0;
            if (oty == 1 || oty == 2) {
                o->weaponType = oty;
                o->thrown     = true;
                o->groundY    = f.z;
                o->ephemeral  = true;
            }
        }
    }
}

// Apply an attack box to every weapon lying around: durability comes from the
// weapon's own <weapon_hp>, and at 0 it shatters into broken_weapon.dat (oid 999).
static void damageObjects(const HitInfo& hi, bool fromRight, int skipIdx,
                          bool attackerArmed, int& attackerArest) {
    for (int i = 0; i < g_objects.SIZE; i++) {
        if (i == skipIdx) continue;                 // never your own held weapon
        lf2::Object& o = g_objects.objs[i];
        if (!o.active || o.weaponType <= 0 || o.rehit > 0) continue;
        // OpenLF2 class_global.c:235-242: an attack on a victim whose state is
        // on_ground_state_1 (1004 — a LIGHT weapon lying down) fails unless the
        // attacker is an object or is holding a weapon. So a bare fist cannot
        // smash a knife on the floor, while a stone/box (state 2004) can be hit
        // freely. We were letting punches break anything on the ground.
        if (!attackerArmed && o.f.state() == lf2::weapon_state::LIGHT_ON_GROUND)
            continue;
        bool hit = false;
        o.f.forEachBdy([&](const lf2::WBox& wb, const dat::Bdy&) {
            if (!hit && boxOverlap(hi.box, toBox(wb))) hit = true;
        });
        if (!hit) continue;
        // `o` e' a ARMA que apanhou — a vitima. Logo `o.rehit` e' o contador POR
        // PAR (segundo slot), e o escalar do atacante entra no primeiro.
        // Uma arma e' um objeto com file->type 1/2/3 e entra na mesma varredura
        // de vitimas que os personagens (lf2.exe FUN_00417400), e a gravacao do
        // arest em 0x0042f2f7 e' incondicional quanto ao tipo da vitima: socar
        // uma pedra consome o arest do atacante no original.
        applyRest(hi, attackerArest, o.rehit);
        if (!o.takeHit(hi.injury, fromRight)) continue;
        // Broke: swap it for the shatter effect at the same spot.
        float bx = o.f.x, by = o.f.y, bz = o.f.z;
        bool  br = o.f.facingRight;
        o.active = false;
        if (ObjAssets* oa = objAssetsCached(999)) {
            if (lf2::Object* fx = g_objects.alloc())
                fx->spawn(&oa->data, objBankIndex(oa), bx, by, bz, br, 0, /*team=*/0);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD
// ─────────────────────────────────────────────────────────────────────────────
static void renderHUD(SDL_Renderer* r, const lf2::Player& player, int playerIdx,
                      EnemySlot slots[], bool auditMode = false)
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

    // Audit mode marker: a yellow strip across the top of the panel (no font in
    // the engine yet, so the state has to be readable as a shape).
    if (auditMode) fillRect(r, 0, 0, SCREEN_W, 4, 250, 210, 40);

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

// AUDIT MODE helper. The canonical hit_ table lives on the standing frame, so a
// fighter can be driven through every special it owns regardless of what it is
// doing now. Returns the frame id fired, or 0 when that slot is empty.
static int auditFire(lf2::Player& p, int slot) {
    if (!p.f.data || !p.alive()) return 0;
    const dat::Frame* st = p.f.data->frame(lf2::fid::STANDING);
    if (!st) return 0;
    const int ids[6] = { st->hit_Fa, st->hit_Ua, st->hit_Da,
                         st->hit_Fj, st->hit_Uj, st->hit_Dj };
    int id = ids[slot % 6];
    if (id <= 0 || !p.f.data->frame(id)) return 0;
    p.f.mp = p.f.maxMp;               // MP is not what we are auditing
    p.f.vx = 0.f;
    p.f.setFrame(id);
    ++p.swingId;
    p.syncAnchor();
    return id;
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
#ifdef LF2_HEADLESS
    ++g_resets;
#endif

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
int main(int argc, char* argv[]) {
#ifdef LF2_HEADLESS
    // ./bin/lf2-headless [ticks]  — default 60 s of simulated play.
    if (argc > 1) g_headlessTicks = std::atol(argv[1]);
    // 2nd arg: roster index. The pool-starvation bug only reproduces with a
    // fighter whose opoints emit WEAPONS (Henry's arrows, Rudolf's shuriken) —
    // Dennis only throws balls, which despawn off-map on their own.
    if (argc > 2) { g_headlessChar = std::atoi(argv[2]); }
    if (g_headlessChar < 0 || g_headlessChar >= ROSTER_N) g_headlessChar = 0;
    std::printf("harness: %s, %ld ticks\n", ROSTER[g_headlessChar], g_headlessTicks);
#else
    (void)argc; (void)argv;
#endif
    lf2::logOpen(LOG_PATH);
    lf2::logLine("INFO", "LF2 Vita v0.8.0 iniciando");

    // Os tres retornos abaixo nunca eram verificados. Falha em qualquer um deles
    // dava tela preta sem diagnostico nenhum.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        lf2::logLine("ERRO", "SDL_Init: %s", SDL_GetError());
        lf2::logClose();
        return 1;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        lf2::logLine("ERRO", "IMG_Init sem suporte a PNG: %s", IMG_GetError());
        SDL_Quit(); lf2::logClose();
        return 1;
    }

    g_index = dat::loadIndex((APP + "data/data.txt").c_str());
    for (int i = 0; i < ROSTER_N; i++) {
        g_chars[i].data = dat::load((APP + "data/" + ROSTER[i] + ".dat").c_str());
        // Um roster com personagem vazio so' aparecia como boneco imovel em jogo.
        LF2_WARN(!g_chars[i].data.frames.empty(),
                 "roster[%d] (%s) carregou sem frames", i, ROSTER[i]);
    }

    SDL_Joystick* joy = nullptr;
    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

    SDL_Window*   win = SDL_CreateWindow("LF2 Vita v0.8.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!win) {
        lf2::logLine("ERRO", "SDL_CreateWindow: %s", SDL_GetError());
        IMG_Quit(); SDL_Quit(); lf2::logClose();
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) {
        // Vale tentar o software renderer antes de desistir: num device com o
        // driver acelerado indisponivel isso e' a diferenca entre jogar e nao abrir.
        lf2::logLine("AVISO", "SDL_CreateRenderer acelerado falhou (%s); tentando software",
                     SDL_GetError());
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        lf2::logLine("ERRO", "SDL_CreateRenderer: %s", SDL_GetError());
        SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit(); lf2::logClose();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Scene scene;
    scene.load(ren, (APP + "bg/sys/lf/bg.dat").c_str());   // Lion Forest (only stage wired so far)
    // Faces up-front (small): the select screen needs all of them.
    for (int i = 0; i < ROSTER_N; i++)
        g_chars[i].face = loadTex(ren,
            (APP + "assets/" + ROSTER[i] + "_f.png").c_str(), false);

    lf2::Player player;
    EnemySlot   slots[NUM_ENEMIES];
    GameSt gameSt        = GameSt::MENU;
#ifdef LF2_HEADLESS
    int    menuCursor    = g_headlessChar;   // harness picks the fighter
#else
    int    menuCursor    = 0;
#endif
    int    playerIdx     = 0;
    int    gameOverTimer = 0;
    bool   playerWon     = false;
    int    lastSwingId   = -1;
    // arest do JOGADOR — o escalar de objeto+0xEC do original. Trava o golpe
    // inteiro contra todas as vítimas; o vrest por par vive em Enemy::rehitTimer.
    int    playerArest   = 0;
    int    playerPrevFrame = -1;

    bool prevStart = false, prevSel = false;
    // Audit mode state (Select). auditSlot walks the hit_ table below.
    bool auditMode  = false;
    int  auditTimer = 0, auditSlot = 0;
    int  auditDownTicks[NUM_ENEMIES] = {0};
    bool aiEnabled = false;   // TEST MODE default: enemies frozen until Start
    bool prevAtk = false, prevJmp = false, prevL = false, prevR = false,
         prevU = false, prevD = false;

    // Fixed-step accumulator with a ceiling. The old form was
    // `while (now >= nextTick) { …; nextTick += TICK_MS; }`, which never gives up:
    // if one tick ever costs more than TICK_MS (a texture load, a long frame),
    // nextTick falls permanently behind and the loop keeps simulating to catch
    // up — the spiral of death, where the game slows down for good.
#ifndef LF2_HEADLESS
    constexpr Uint32 MAX_FRAME_MS = 250;   // ignore stalls longer than this
#endif
    constexpr int    MAX_STEPS    = 5;     // then drop the backlog instead of chasing
    Uint32 prevMs   = SDL_GetTicks();
    Uint32 accumMs  = 0;
    bool   running  = true;
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT) running = false;

        Uint32 now = SDL_GetTicks();
#ifdef LF2_HEADLESS
        // Run as fast as the CPU allows: one simulated tick per iteration.
        // Wall-clock pacing would make a 60 s scenario take 60 s to test.
        (void)now; (void)prevMs;
        Uint32 dt = (Uint32)TICK_MS;
#else
        Uint32 dt  = now - prevMs;
        prevMs = now;
        if (dt > MAX_FRAME_MS) dt = MAX_FRAME_MS;
#endif
        accumMs += dt;
        int steps = 0;
        while (accumMs >= (Uint32)TICK_MS && steps < MAX_STEPS) {
            accumMs -= (Uint32)TICK_MS;
            ++steps;
#ifdef LF2_HEADLESS
            // Watch the invariants that only break under sustained play — the
            // ones that cost device round-trips: pool starvation and objects
            // that stop animating (a recycled slot stuck as a fake weapon).
            ++g_tickNo;
            {
                int live = 0;
                g_objects.forEach([&](lf2::Object&){ ++live; });
                if (live > g_peakObjects) g_peakObjects = live;
                if (live >= g_objects.SIZE) ++g_poolFullTicks;
                if (gameSt == GameSt::PLAYING) ++g_playTicks;
            }
            if (g_tickNo >= g_headlessTicks) running = false;
#endif
            InputState raw = readInput(joy);
            bool atk  = raw.atk && !prevAtk;
            bool jmp  = raw.jmp && !prevJmp;
            bool newL = raw.L && !prevL, newR = raw.R && !prevR;
            bool newU = raw.U && !prevU, newD = raw.D && !prevD;
            bool newStart = raw.start && !prevStart;
            bool newSel   = raw.sel && !prevSel;
            prevAtk = raw.atk; prevJmp = raw.jmp; prevStart = raw.start;
            prevSel = raw.sel;
            prevL = raw.L; prevR = raw.R; prevU = raw.U; prevD = raw.D;

            // TEST MODE: enemies spawn frozen (they still react to hits); Start
            // toggles their AI so specials can be tested on stationary targets.
            if (newStart) {
                aiEnabled = !aiEnabled;
                for (int i = 0; i < NUM_ENEMIES; i++) slots[i].e.frozen = !aiEnabled;
            }
            // AUDIT MODE (Select): every fighter walks its own hit_ table, one
            // special every AUDIT_PERIOD ticks, with MP topped up. Lets a special
            // be verified without executing its input, which is the whole point:
            // "does the move break?" is a different question from "can I do it?".
            if (newSel) {
                auditMode = !auditMode; auditTimer = 0; auditSlot = 0;
                if (auditMode && gameSt == GameSt::PLAYING) {
                    // Set up a test arena. The enemies spawn at x 1600/2200/2800
                    // on three different z rows, and nothing walks in audit mode —
                    // so every special was firing into empty space 1200 px away,
                    // which reads as "specials do no damage". Line them up at
                    // melee / short / long range ON the player's z row instead.
                    const float dist[NUM_ENEMIES] = { 110.f, 260.f, 430.f };
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        lf2::Player& e = slots[i].e.a;
                        e.x = player.x + dist[i];
                        e.z = player.z;                 // same z: the 15 px band
                        e.right = false;                // face the player
                        e.clampPos(); e.syncAnchor();
                        slots[i].e.frozen = true;       // stand still to be hit
                    }
                    player.right = true;
                    aiEnabled = false;
                }
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
                    playerArest = 0;
                    playerPrevFrame = -1;
                    aiEnabled = false;   // every match starts with frozen enemies
                }
            }
            else if (gameSt == GameSt::PLAYING) {
                float prevX = player.x, prevZ = player.z;   // for the itr-14 block
                int   prevState = player.f.state();         // pre-tick: run vs pickup
                player.tick(raw.L, raw.R, raw.U, raw.D, atk, jmp, raw.def, raw.spc);

                // AUDIT MODE: drive every fighter through its hit_ table. The
                // enemies are staggered so their projectiles do not all land on
                // the same tick, which makes it possible to tell them apart.
                if (auditMode) {
                    constexpr int AUDIT_PERIOD = 50;      // ~1.7 s per special
                    if (++auditTimer >= AUDIT_PERIOD) {
                        auditTimer = 0;
                        auditFire(player, auditSlot);
                        for (int i = 0; i < NUM_ENEMIES; i++)
                            auditFire(slots[i].e.a, auditSlot + i + 1);
                        ++auditSlot;
                    }
                    // Revive on death only — topping HP up at 50 % made everyone
                    // effectively immortal AND hid the damage being audited: a
                    // special would land and the bar would refill before you saw
                    // it move. Now the bars behave normally and a KO just resets
                    // the fighter, so the session still never ends.
                    auto revive = [](lf2::Player& p) {
                        if (p.f.hp > 0) return;
                        p.f.hp = p.maxHp();
                        p.f.mp = p.f.maxMp;
                        p.knockedDown = false;
                        p.fall = 0;
                        p.h = 0.f; p.vy = 0.f;
                        p.f.setFrame(lf2::fid::STANDING);
                        p.syncAnchor();
                    };
                    revive(player);
                    for (int i = 0; i < NUM_ENEMIES; i++) revive(slots[i].e.a);

                    // Do NOT zero the falling points every tick: that made the
                    // targets un-knockdownable and hid the very thing being
                    // audited (a fall:70 special looked like it did nothing).
                    // Instead, let knockdowns happen and stand the target back up
                    // after it has been down for a moment, so the next special in
                    // the cycle still finds someone upright.
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        lf2::Player& e = slots[i].e.a;
                        int es = e.f.state();
                        if (e.f.hp > 0 && es == lf2::ST_LYING) {
                            if (++auditDownTicks[i] > 20) {
                                auditDownTicks[i] = 0;
                                e.knockedDown = false; e.fall = 0;
                                e.h = 0.f; e.vy = 0.f; e.f.vx = 0.f;
                                e.f.setFrame(lf2::fid::STANDING);
                                e.syncAnchor();
                            }
                        } else if (es != lf2::ST_FALLING) {
                            auditDownTicks[i] = 0;
                        }
                    }
                }
                for (int i = 0; i < NUM_ENEMIES; i++)
                    slots[i].e.tick(player.x, player.z);

                // ── opoint spawns (on frame entry) ────────────────────────────
                if (player.f.frameId != playerPrevFrame) {
                    spawnOpoints(player.f, 0);
                    playerPrevFrame = player.f.frameId;
                }
                for (int i = 0; i < NUM_ENEMIES; i++) {
                    if (slots[i].e.a.f.frameId != slots[i].prevFrame) {
                        spawnOpoints(slots[i].e.a.f, 1);
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
                // Fire/ice made the player drop what he was holding (F.LF
                // drop_weapon in the effect switch). The Player only raises the
                // flag; the slot lives here.
                if (player.dropWeaponReq) {
                    player.dropWeaponReq = false;
                    if (player.heldWeapon >= 0) {
                        lf2::Object& w = g_objects.objs[player.heldWeapon];
                        if (w.active)
                            w.dropAt(player.x + (player.right ? 30.f : -30.f),
                                     player.z + player.h, player.z, player.right, player.z);
                        player.heldWeapon = -1; player.heavyWeapon = false;
                    }
                }

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
                // SWEPT obstacle test. Testing only the final position tunnels:
                // the stone's kind-14 box is 16 px wide and a run step is 10.5 px,
                // so a running fighter could be clear on both sides of it in
                // consecutive ticks and pass straight through. Walk the movement
                // in short substeps and stop at the last free one.
                {
                    const float mvX = player.x - prevX, mvZ = player.z - prevZ;
                    const float dist = sqrtf(mvX * mvX + mvZ * mvZ);
                    if (dist > 0.01f) {
                        player.x = prevX; player.z = prevZ; player.syncAnchor();
                        // Was the START of the move already overlapping? The swept
                        // walk stops AT contact, so the very next tick starts in
                        // contact — and "already inside → let it through" then
                        // waved the runner straight through the stone (it blocked
                        // for exactly one tick). Escape is now allowed only when
                        // the step moves AWAY from the obstacle's centre.
                        float obsX = 0.f, obsZ = 0.f;
                        bool stuck = blockedByObstacle(player, g_objects,
                                                       player.heldWeapon, &obsX, &obsZ);
                        if (stuck) {
                            // "Away" has to be measured in BOTH axes. Comparing
                            // |x - obsX| alone compared a value with itself on a
                            // pure-depth step (mvX == 0): never away, so the else
                            // reverted x AND z and up/down did nothing while in
                            // contact — the only way off a stone was to back out
                            // along x. mvZ was computed here and never used.
                            const float d0x = prevX - obsX,         d0z = prevZ - obsZ;
                            const float d1x = prevX + mvX - obsX,   d1z = prevZ + mvZ - obsZ;
                            bool away = (d1x * d1x + d1z * d1z) > (d0x * d0x + d0z * d0z);
                            if (away) { player.x = prevX + mvX; player.z = prevZ + mvZ; }
                            else      { player.x = prevX;       player.z = prevZ; }
                        } else {
                            const int steps = (int)(dist / 5.f) + 1;
                            float okX = prevX, okZ = prevZ;
                            for (int s = 1; s <= steps; ++s) {
                                float t = (float)s / (float)steps;
                                player.x = prevX + mvX * t;
                                player.z = prevZ + mvZ * t;
                                player.syncAnchor();
                                if (blockedByObstacle(player, g_objects, player.heldWeapon)) break;
                                okX = player.x; okZ = player.z;
                            }
                            player.x = okX; player.z = okZ;
                        }
                        player.clampPos();
                        player.syncAnchor();
                    }
                }

                // ── new player swing re-arms the enemies ─────────────────────
                if (playerArest > 0) --playerArest;
                if (player.swingId != lastSwingId) {
                    for (int i = 0; i < NUM_ENEMIES; i++) slots[i].e.rehitTimer = 0;
                    playerArest = 0;
                    lastSwingId = player.swingId;
                }

                // ── player attack → enemies ──────────────────────────────────
                HitInfo hi;
                if (playerArest == 0 && fighterAttack(player.f, hi)) {
                    // Collect every legal victim first. An itr with vrest == 0 is
                    // single-target in the original (closest wins), so hitting the
                    // whole overlapping stack with one punch was wrong.
                    int  cand[NUM_ENEMIES], nCand = 0;
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        Box ebody;
                        Enemy& e = slots[i].e;
                        int es = e.a.f.state();
                        // OpenLF2 anti-juggle: a light hit (fall<=40) can't strike
                        // a victim already falling; a launcher (fall>40) still can.
                        bool downed = ((es == lf2::ST_FALLING && hi.fall <= 40) ||
                                       es == lf2::ST_LYING) &&
                                      !itrIgnoresAntiJuggle(hi.kind);
                        // lf2.exe FUN_00417400: the itr's `effect` can veto the
                        // hit outright (fire cannot touch a burning victim).
                        if (!lf2::itrEffectAllows(hi.effect, hi.attState, es,
                                                  e.a.f.frameId, true)) continue;
                        if (e.rehitTimer == 0 && e.alive() && !downed && !e.a.untouchable() &&
                            fabsf(player.z - e.a.z) < (float)hi.zwidth &&
                            fighterBody(e.a.f, ebody) && boxOverlap(hi.box, ebody))
                            cand[nCand++] = i;
                    }
                    if (hi.singleTarget && nCand > 1) {      // keep the closest only
                        int best = cand[0];
                        float bd = fabsf(player.x - slots[best].e.a.x);
                        for (int c = 1; c < nCand; c++) {
                            float d = fabsf(player.x - slots[cand[c]].e.a.x);
                            if (d < bd) { bd = d; best = cand[c]; }
                        }
                        cand[0] = best; nCand = 1;
                    }
                    for (int c = 0; c < nCand; c++) {
                        Enemy& e = slots[cand[c]].e;
                        applyRest(hi, playerArest, e.rehitTimer);
                        e.hitFlash   = 10;
                        float kb = (float)(hi.dvx > 0 ? hi.dvx : 1);
                        e.a.hit(hi.injury, player.right ? kb : -kb, hi.fall, hi.effect,
                                hi.bdefend, player.right);
#ifdef LF2_HEADLESS
                        g_damageDealt += hi.injury;
#endif
                    }
                    // …and the same swing damages WEAPONS lying around: they have
                    // <weapon_hp> durability (stone 800, knife 200) and break at 0.
                    damageObjects(hi, player.right, player.heldWeapon, player.heldWeapon >= 0,
                                  playerArest);
                }

                // ── held weapon's itr → enemies ──────────────────────────────
                // F.LF: a held weapon strikes through its itr KIND 5, and the real
                // damage comes from <weapon_strength_list>[wpoint.attacking]
                // (1 normal · 2 jump · 3 run · 4 dash), not from the itr itself.
                if (player.heldWeapon >= 0) {
                    lf2::Object& w = g_objects.objs[player.heldWeapon];
                    dat::Wpoint hw; fighterWpoint(player.f, hw);
                    HitInfo whi; bool swinging = false;
                    whi.attState = player.f.state();   // for itrEffectAllows()
                    if (w.active && hw.attacking > 0) {
                        w.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
                            if (it.kind != 5 || swinging) return;
                            whi.box = toBox(wb);
                            whi.injury = it.injury; whi.fall = it.fall;
                            whi.dvx = it.dvx;
                            whi.arest = it.arest; whi.vrest = it.vrest;
                            whi.bdefend = it.bdefend;
                            whi.zwidth = it.zwidth > 0 ? it.zwidth : 15;
                            whi.kind   = it.kind;
                            // vrest == 0 is single-target here too. This path knew
                            // it (the `rest` line right above reads vrest) and then
                            // swung through the whole overlapping stack anyway —
                            // one club swing floored every enemy standing together.
                            whi.singleTarget = (it.vrest == 0);
                            swinging = true;
                        });
                        const dat::File* wd = w.f.data;
                        if (swinging && wd && hw.attacking < 8 && wd->strength[hw.attacking].valid) {
                            const dat::StrengthEntry& se = wd->strength[hw.attacking];
                            whi.injury = se.injury; whi.fall = se.fall; whi.dvx = se.dvx;
                            whi.effect = se.effect;   // weapon7 (ice sword) is effect 3
                            if (se.vrest > 0) whi.vrest = se.vrest;
                            if (se.arest > 0) whi.arest = se.arest;
                        }
                    }
                    if (swinging && playerArest == 0) {
                        damageObjects(whi, player.right, player.heldWeapon, /*attackerArmed=*/true,
                                      playerArest);
                        // Same two-pass shape as the melee path: collect the legal
                        // victims, then narrow to the closest if the itr is
                        // single-target.
                        int  wcand[NUM_ENEMIES], nwCand = 0;
                        for (int i = 0; i < NUM_ENEMIES; i++) {
                            Box ebody; Enemy& e = slots[i].e;
                            int wes = e.a.f.state();
                            bool wDowned = ((wes == lf2::ST_FALLING && whi.fall <= 40) ||
                                            wes == lf2::ST_LYING) &&
                                           !itrIgnoresAntiJuggle(whi.kind);
                            if (!lf2::itrEffectAllows(whi.effect, whi.attState, wes,
                                                      e.a.f.frameId, true)) continue;
                            if (e.rehitTimer == 0 && e.alive() && !wDowned &&
                                !e.a.untouchable() &&
                                fabsf(player.z - e.a.z) < (float)whi.zwidth &&
                                fighterBody(e.a.f, ebody) && boxOverlap(whi.box, ebody))
                                wcand[nwCand++] = i;
                        }
                        if (whi.singleTarget && nwCand > 1) {
                            int best = wcand[0];
                            float bd = fabsf(player.x - slots[best].e.a.x);
                            for (int c = 1; c < nwCand; c++) {
                                float d = fabsf(player.x - slots[wcand[c]].e.a.x);
                                if (d < bd) { bd = d; best = wcand[c]; }
                            }
                            wcand[0] = best; nwCand = 1;
                        }
                        for (int c = 0; c < nwCand; c++) {
                            Enemy& e = slots[wcand[c]].e;
                            applyRest(whi, playerArest, e.rehitTimer);
                            e.hitFlash   = 10;
                            float kb = (float)(whi.dvx > 0 ? whi.dvx : 4);
                            e.a.hit(whi.injury, player.right ? kb : -kb, whi.fall, whi.effect,
                                    whi.bdefend, player.right);
#ifdef LF2_HEADLESS
                            // The held-weapon swing counts toward the harness's
                            // damage figure too. It did not, which left the whole
                            // <weapon_strength_list> path — injury, fall, dvx and
                            // effect — invisible to the number used as the
                            // regression signal.
                            g_damageDealt += whi.injury;
#endif
                        }
                    }
                }

                // ── enemy attacks → player ───────────────────────────────────
                Box pBody;
                bool havePBody = player.alive() && !player.untouchable() && fighterBody(player.f, pBody);
                if (havePBody) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        HitInfo ehi;
                        Enemy& e = slots[i].e;
                        // fighterAttack FIRST: the z test used to be evaluated
                        // before it, so `ehi.zwidth` was still the default 15 and
                        // every wide attack (henry has itrs with zwidth 76/55/34)
                        // was silently narrowed. && short-circuits left to right.
                        // The anti-juggle test HAS to stay inline, after
                        // fighterAttack has filled ehi — hoisting it into a
                        // variable above the call is the very evaluation-order
                        // trap this block was rewritten to close (it read
                        // ehi.fall while ehi was still default-constructed). The
                        // hoisted copy used to sit here, dead, cast away with a
                        // (void); it is gone rather than left as a loaded gun.
                        int ps3 = player.f.state();
                        if (!e.hasHitPlayer && e.alive() && !player.untouchable() &&
                            fighterAttack(e.a.f, ehi) &&
                            lf2::itrEffectAllows(ehi.effect, ehi.attState, ps3,
                                                 player.f.frameId, true) &&
                            !(((ps3 == lf2::ST_FALLING && ehi.fall <= 40) ||
                               ps3 == lf2::ST_LYING) && !itrIgnoresAntiJuggle(ehi.kind)) &&
                            fabsf(player.z - e.a.z) < (float)ehi.zwidth &&
                            boxOverlap(ehi.box, pBody))
                        {
                            e.hasHitPlayer = true;
                            float kb = (float)(ehi.dvx > 0 ? ehi.dvx : 1);
                            player.hit(ehi.injury, e.a.right ? kb : -kb, ehi.fall, ehi.effect,
                                       ehi.bdefend, e.a.right);
                        }
                    }
                }

                // ── projectiles hit actors ───────────────────────────────────
                g_objects.forEach([&](lf2::Object& o) {
                    // What decides whether an object can hit is its DATA: the
                    // current frame either carries an attack itr or it does not.
                    // Gating on a whitelist of states kept missing cases —
                    // firen_flame flies in state 18 ("burning"), so the flame went
                    // right through everyone. The spent states (hiting/hit/
                    // rebounding) drop their itr in the .dat, so they stop hurting
                    // on their own, exactly like the original.
                    if (o.arest > 0) return;      // arest trava o objeto inteiro
                    HitInfo ohi;
                    if (!fighterAttack(o.f, ohi)) return;
                    // Re-hit is tracked PER VICTIM, not once for the whole object.
                    // lf2.exe keeps an arest/vrest array indexed by the victim's
                    // object id (object+0xb8 …), which is what lets a piercing
                    // ball (state 3006) go on to the next body in the same pass
                    // while still respecting vrest against the one it just hit.
                    // A single shared counter made every ball single-target.
                    if (o.team == 0) {                       // player's ball → enemies
                        for (int i = 0; i < NUM_ENEMIES; i++) {
                            Box ebody;
                            Enemy& e = slots[i].e;
                            if (o.victimRest[i] > 0) continue;
                            // Same anti-juggle the melee path uses: a light hit
                            // cannot strike someone already falling, and nobody
                            // hits a body on the floor. Without this, projectiles
                            // were the only thing that damaged a downed fighter —
                            // which read as "only the balls and the wind hurt".
                            int es = e.a.f.state();
                            bool downed = ((es == lf2::ST_FALLING && ohi.fall <= 40) ||
                                           es == lf2::ST_LYING) &&
                                          !itrIgnoresAntiJuggle(ohi.kind);
                            if (!lf2::itrEffectAllows(ohi.effect, ohi.attState, es,
                                                      e.a.f.frameId, true)) continue;
                            if (e.alive() && !downed && !e.a.untouchable() &&
                                fabsf(o.f.z - e.a.z) < (float)ohi.zwidth &&
                                fighterBody(e.a.f, ebody) && boxOverlap(ohi.box, ebody))
                            {
                                float kb = (float)(ohi.dvx > 0 ? ohi.dvx : 4);
                                e.a.hit(ohi.injury, o.f.facingRight ? kb : -kb, ohi.fall, ohi.effect,
                                        ohi.bdefend, o.f.facingRight);
#ifdef LF2_HEADLESS
                                g_damageDealt += ohi.injury;
#endif
                                e.hitFlash = 10;
                                applyRest(ohi, o.arest, o.victimRest[i]);
                                o.onHit();
                                // A ball that spends itself on the first body
                                // (state 3000 → `hiting`) stops here; a piercing
                                // one keeps scanning the rest of the line.
                                if (o.spent()) break;
                            }
                        }
                    } else if (havePBody) {                  // enemy ball → player
                        int ps2 = player.f.state();
                        bool pDowned = ((ps2 == lf2::ST_FALLING && ohi.fall <= 40) ||
                                        ps2 == lf2::ST_LYING) &&
                                       !itrIgnoresAntiJuggle(ohi.kind);
                        if (o.victimRest[NUM_ENEMIES] == 0 && !pDowned &&
                            lf2::itrEffectAllows(ohi.effect, ohi.attState, ps2,
                                                 player.f.frameId, true) &&
                            fabsf(o.f.z - player.z) < (float)ohi.zwidth && boxOverlap(ohi.box, pBody)) {
                            float kb = (float)(ohi.dvx > 0 ? ohi.dvx : 4);
                            player.hit(ohi.injury, o.f.facingRight ? kb : -kb, ohi.fall, ohi.effect,
                                       ohi.bdefend, o.f.facingRight);
                            applyRest(ohi, o.arest, o.victimRest[NUM_ENEMIES]);
                            o.onHit();
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
        }
        // Hit the step ceiling → we are behind. Throw the backlog away instead of
        // trying to replay it; a dropped frame beats a permanent slow-motion.
        if (steps == MAX_STEPS) accumMs = 0;

        if (gameSt == GameSt::MENU) {
            renderMenu(ren, menuCursor);
        } else {
            int camX = clampI((int)player.x - SCREEN_W / 2, 0, MAP_W - SCREEN_W);
            renderBackground(ren, scene, camX);

            // Shadows — size from bg.dat's shadowsize (Lion Forest: 37×9).
            if (scene.shadow) {
                const int shW = scene.bg.shadowW, shH = scene.bg.shadowH;
                const int shOx = shW / 2;
                // A vanished fighter (next: 1280) hides sprite AND shadow; over
                // the last few ticks the shadow blinks back as a tell.
                bool pShadow = !player.hidden() ||
                               (player.shadowBlink() && ((player.vanish / 2) % 2 == 0));
                if (pShadow) {
                    SDL_Rect ps = { (int)player.x - camX - shOx, (int)player.z - shH/2, shW, shH };
                    SDL_RenderCopy(ren, scene.shadow, nullptr, &ps);
                }
                for (int i = 0; i < NUM_ENEMIES; i++) {
                    const lf2::Player& ea = slots[i].e.a;
                    bool sh = !ea.hidden() || (ea.shadowBlink() && ((ea.vanish / 2) % 2 == 0));
                    if (!sh) continue;
                    SDL_Rect es = { (int)ea.x - camX - shOx, (int)ea.z - shH/2, shW, shH };
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
            DrawRef list[NUM_ENEMIES + 1 + g_objects.SIZE];
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
                    if (!player.hidden())
                        drawFighter(ren, player.f, g_chars[playerIdx].sheets, camX);
                } else if (list[i].kind == 1) {
                    EnemySlot& s = slots[list[i].idx];
                    if (s.e.a.hidden()) continue;
                    bool flash = s.e.hitFlash > 0;
                    drawFighter(ren, s.e.a.f, g_chars[s.rosterIdx].sheets, camX,
                                255, flash ? 80 : 255, flash ? 80 : 255);
                } else {
                    lf2::Object& o = g_objects.objs[list[i].idx];
                    if (o.sheetSlot >= 0 && o.sheetSlot < (int)g_objBank.size())
                        drawFighter(ren, o.f, g_objBank[o.sheetSlot].sheets, camX);
                }
            }

            renderHUD(ren, player, playerIdx, slots, auditMode);
            if (gameSt == GameSt::GAMEOVER)
                renderGameOver(ren, playerWon, gameOverTimer);
        }

        SDL_RenderPresent(ren);
    }

#ifdef LF2_HEADLESS
    {
        int live = 0;
        g_objects.forEach([&](lf2::Object&){ ++live; });
        std::printf("\n--- headless run ---\n");
        std::printf("ticks simulados      : %ld (%.1f s a 30 Hz)\n",
                    g_tickNo, (double)g_tickNo / 30.0);
        std::printf("pico do object pool  : %d / %d\n", g_peakObjects, g_objects.SIZE);
        std::printf("ticks com pool cheio : %ld\n", g_poolFullTicks);
        std::printf("spawns criados       : %ld (partidas: %ld)\n", g_spawnTotal, g_resets);
        std::printf("spawns descartados   : %ld %s\n", g_spawnDrops,
                    g_spawnDrops ? "<<< ESPECIAL SEM PROJETIL" : "");
        std::printf("ticks em combate     : %ld %s\n", g_playTicks,
                    g_playTicks < g_tickNo / 4 ? "<<< quase nao jogou" : "");
        std::printf("objetos vivos no fim : %d\n", live);
        std::printf("banco de assets      : %d entradas\n", (int)g_objBank.size());
        std::printf("dano causado (HP)    : %ld %s\n", g_damageDealt,
                    g_damageDealt == 0 ? "<<< NENHUM GOLPE ACERTOU" : "");
        bool bad = (g_poolFullTicks > 0) || (g_spawnDrops > 0);
        std::printf("resultado            : %s\n", bad ? "FALHA" : "ok");
        if (bad) { SDL_Quit(); return 1; }
    }
#endif
    if (joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    lf2::logLine("INFO", "saida normal");
    lf2::logClose();
#ifndef LF2_HOST
    sceKernelExitProcess(0);
#endif
    return 0;
}
