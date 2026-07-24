#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <psp2/kernel/processmgr.h>
#include <cmath>

#include "engine/types.hpp"
#include "engine/render.hpp"
#include "engine/dat.hpp"
#include "engine/fighter.hpp"
#include "characters/player.hpp"   // lf2::Player — frame-driven actor (player)
#include "characters/enemy.hpp"    // Enemy = lf2::Player + pursuit AI

// ─────────────────────────────────────────────────────────────────────────────
//  Character data — loaded once from the original .dat, bundled in the VPK.
//  Player and enemies are both data-driven now; add more characters by loading
//  more .dat files here.
// ─────────────────────────────────────────────────────────────────────────────
static dat::File g_dennis;
static dat::File g_firen;

// ─────────────────────────────────────────────────────────────────────────────
//  Collision helpers — work on any lf2::Player (player OR enemy actor)
// ─────────────────────────────────────────────────────────────────────────────
static inline Box toBox(const lf2::WBox& w) {
    return { (int)w.x, (int)w.y, (int)w.w, (int)w.h };
}

// First attacking itr (kind 0) of the actor's current frame, in world space.
// Only present during attack frames, so it doubles as "is this actor striking?".
struct HitInfo {
    Box box;
    int injury = 20;   // damage
    int fall   = -1;   // knockdown weight
    int dvx    = 0;    // victim knockback (facing-signed by the caller)
    int rest   = 8;    // re-hit delay: vrest (per victim) or arest, default 8 ticks
};
static bool actorAttack(const lf2::Player& p, HitInfo& out) {
    bool found = false;
    p.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
        if (it.kind == 0 && !found) {
            out.box    = toBox(wb);
            out.injury = it.injury > 0 ? it.injury : 20;
            out.fall   = it.fall;
            out.dvx    = it.dvx;
            out.rest   = it.vrest > 0 ? it.vrest : (it.arest > 0 ? it.arest : 8);
            found = true;
        }
    });
    return found;
}

// First body box of the actor's current frame, in world space.
static bool actorBody(const lf2::Player& p, Box& out) {
    bool found = false;
    p.f.forEachBdy([&](const lf2::WBox& wb, const dat::Bdy&) {
        if (!found) { out = toBox(wb); found = true; }
    });
    return found;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input helpers (PS Vita joystick → bool directions / buttons)
// ─────────────────────────────────────────────────────────────────────────────
struct InputState { bool L, R, U, D, atk, jmp, def, spc, any; };

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
    in.spc = SDL_JoystickGetButton(joy, BTN_SPECIAL);
    in.any = in.atk || in.jmp || in.def || in.spc || in.L || in.R || in.U || in.D;
    return in;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Textures
// ─────────────────────────────────────────────────────────────────────────────
struct Textures {
    SDL_Texture* forests  = nullptr;
    SDL_Texture* forestm1 = nullptr;
    SDL_Texture* forestm2 = nullptr;
    SDL_Texture* forestm3 = nullptr;
    SDL_Texture* forestm4 = nullptr;
    SDL_Texture* foresett = nullptr;
    SDL_Texture* land1    = nullptr;
    SDL_Texture* land2    = nullptr;
    SDL_Texture* land4    = nullptr;
    // Character sheets, indexed by the .dat's declared file() order:
    //   0 → pics 0-69, 1 → pics 70-139, 2 → pics 140-209
    SDL_Texture* dennis[3] = { nullptr, nullptr, nullptr };  // magenta key
    SDL_Texture* firen[3]  = { nullptr, nullptr, nullptr };  // black key
    SDL_Texture* shadow   = nullptr;

    void load(SDL_Renderer* r) {
        forests  = loadTex(r, "app0:/assets/forests.png",  false);
        // Forest layers ship with BLACK transparent backgrounds (not the magenta
        // the Dennis sheets use), so they need a black color key.
        forestm1 = loadTex(r, "app0:/assets/forestm1.png", true, 0, 0, 0);
        forestm2 = loadTex(r, "app0:/assets/forestm2.png", true, 0, 0, 0);
        forestm3 = loadTex(r, "app0:/assets/forestm3.png", true, 0, 0, 0);
        forestm4 = loadTex(r, "app0:/assets/forestm4.png", true, 0, 0, 0);
        foresett = loadTex(r, "app0:/assets/forestt.png",  true, 0, 0, 0);
        // land1/land4 ship with black transparent backgrounds (land2 is green-
        // backed): drawing them opaque punches black holes in the ground.
        land1    = loadTex(r, "app0:/assets/land1.png",    true, 0, 0, 0);
        land2    = loadTex(r, "app0:/assets/land2.png",    false);
        land4    = loadTex(r, "app0:/assets/land4.png",    true, 0, 0, 0);
        // All sheets now come straight from the ORIGINAL LF2 BMPs, whose
        // transparent background is pure BLACK (the old magenta sheets were
        // pre-processed exports and are gone).
        dennis[0] = loadTex(r, "app0:/assets/dennis_0.png", true, 0, 0, 0);
        dennis[1] = loadTex(r, "app0:/assets/dennis_1.png", true, 0, 0, 0);
        dennis[2] = loadTex(r, "app0:/assets/dennis_2.png", true, 0, 0, 0);
        firen[0]  = loadTex(r, "app0:/assets/firen_0.png",  true, 0, 0, 0);
        firen[1]  = loadTex(r, "app0:/assets/firen_1.png",  true, 0, 0, 0);
        firen[2]  = loadTex(r, "app0:/assets/firen_2.png",  true, 0, 0, 0);
        shadow    = loadTex(r, "app0:/assets/s.png", true, 0, 0, 0); // ellipse on black
    }
};

// Draw one frame-driven actor from its own sheets, at its .dat sprite origin.
static void drawActor(SDL_Renderer* r, const lf2::Player& a, SDL_Texture* const sheets[3],
                      int camX, Uint8 cr = 255, Uint8 cg = 255, Uint8 cb = 255)
{
    int ord, loc;
    if (!a.f.sheetLocal(ord, loc) || ord < 0 || ord >= 3 || !sheets[ord]) return;
    float dx, dy;
    a.f.drawOrigin(dx, dy);
    drawSprite(r, sheets[ord], loc, (int)dx - camX, (int)dy, !a.right, cr, cg, cb);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render: background layers
// ─────────────────────────────────────────────────────────────────────────────
static void renderBackground(SDL_Renderer* r, const Textures& tx, int camX) {
    SDL_SetRenderDrawColor(r, 111, 163, 218, 255);
    SDL_RenderClear(r);

    drawTiled(r, tx.forests,   128, 800,  70, camX);
    drawOnce (r, tx.forestm1,    0, 147, 800, 104, camX);
    drawOnce (r, tx.forestm2,  800, 147, 300, 104, camX);
    drawOnce (r, tx.forestm3,    0, 170, 284,  84, camX);
    drawOnce (r, tx.forestm4, 1216, 155, 184,  87, camX);
    drawTiled(r, tx.foresett,  199, 253, 162, camX);

    fillRect(r, 0, 356, SCREEN_W, 172, 16, 77, 16);
    drawTiled(r, tx.land1, 356, 175,  74, camX);
    drawTiled(r, tx.land2, 385, 225,  89, camX);
    drawTiled(r, tx.land4, 420, 206, 106, camX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render: actors sorted by Z (painter's algorithm)
// ─────────────────────────────────────────────────────────────────────────────
static void renderCharacters(SDL_Renderer* r, const Textures& tx,
                             const lf2::Player& player, Enemy enemies[], int camX)
{
    if (tx.shadow) {
        // player.x is the anchor (sprite middle). Shadow at its native size from
        // the bg.dat spec (shadowsize: 37 9), centered under the anchor.
        SDL_Rect ps = { (int)player.x - camX - 18, (int)player.z - 4, 37, 9 };
        SDL_RenderCopy(r, tx.shadow, nullptr, &ps);
        for (int i = 0; i < NUM_ENEMIES; i++) {
            SDL_Rect es = { (int)enemies[i].a.x - camX - 18, (int)enemies[i].a.z - 4, 37, 9 };
            SDL_RenderCopy(r, tx.shadow, nullptr, &es);
        }
    }

    struct Drawable { float z; int type; int idx; };
    Drawable dlist[NUM_ENEMIES + 1];
    dlist[0] = { player.z, 0, 0 };
    for (int i = 0; i < NUM_ENEMIES; i++) dlist[i + 1] = { enemies[i].a.z, 1, i };

    for (int i = 1; i < NUM_ENEMIES + 1; i++) {
        Drawable key = dlist[i]; int j = i - 1;
        while (j >= 0 && dlist[j].z > key.z) { dlist[j + 1] = dlist[j]; j--; }
        dlist[j + 1] = key;
    }

    for (int i = 0; i < NUM_ENEMIES + 1; i++) {
        if (dlist[i].type == 0) {
            drawActor(r, player, tx.dennis, camX);
        } else {
            int k = dlist[i].idx;
            bool flash = enemies[k].hitFlash > 0;
            drawActor(r, enemies[k].a, tx.firen, camX,
                      255, flash ? 80 : 255, flash ? 80 : 255);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render: HUD (HP bars + portraits)
// ─────────────────────────────────────────────────────────────────────────────
static void renderHUD(SDL_Renderer* r, const Textures& tx,
                      const lf2::Player& player, Enemy enemies[])
{
    constexpr int HUD_H   = 72;
    constexpr int SLOTS   = NUM_ENEMIES + 1;
    constexpr int SW_SLOT = SCREEN_W / SLOTS;
    constexpr int PORT_W  = 40, PORT_H = 56;
    constexpr int BAR_X   = PORT_W + 6;

    fillRect(r, 0, 0, SCREEN_W, HUD_H, 28, 56, 130);
    fillRect(r, 0, HUD_H, SCREEN_W, 2, 10, 20, 70);
    for (int i = 1; i < SLOTS; i++)
        fillRect(r, i * SW_SLOT - 1, 2, 2, HUD_H - 4, 10, 20, 80);

    fillRect(r, 2, 2, PORT_W, PORT_H, 18, 36, 90);
    drawSpriteAt(r, tx.dennis[0], 0, 2, 2, PORT_W, PORT_H, false);
    drawHpBar(r, BAR_X, 10, player.hp(), player.maxHp(), 210, 40,  40);
    drawHpBar(r, BAR_X, 28,           0, player.maxHp(),  40, 110, 210);

    for (int i = 0; i < NUM_ENEMIES; i++) {
        int sx = (i + 1) * SW_SLOT;
        fillRect(r, sx + 2, 2, PORT_W, PORT_H, 18, 36, 90);
        drawSpriteAt(r, tx.firen[0], 0, sx + 2, 2, PORT_W, PORT_H, true);
        drawHpBar(r, sx + BAR_X, 10, enemies[i].a.hp(), enemies[i].a.maxHp(), 210, 40,  40);
        drawHpBar(r, sx + BAR_X, 28,                 0, enemies[i].a.maxHp(),  40, 110, 210);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render: menu / game-over
// ─────────────────────────────────────────────────────────────────────────────
static void renderMenu(SDL_Renderer* r) {
    fillRect(r, 0, 0, SCREEN_W, SCREEN_H, 10, 10, 40);
    fillRect(r, SCREEN_W/2 - 240, 180, 480,  70, 220, 170,   0);
    fillRect(r, SCREEN_W/2 - 236, 184, 472,  62,  40,  20,   0);
    fillRect(r, SCREEN_W/2 - 120, 193,  70,  44, 220, 170,   0);
    fillRect(r, SCREEN_W/2 -  30, 193,  70,  44, 220, 170,   0);
    fillRect(r, SCREEN_W/2 +  60, 193,  70,  44, 220, 170,   0);
    if ((SDL_GetTicks() / 500) % 2 == 0)
        fillRect(r, SCREEN_W/2 - 120, 340, 240, 24, 255, 255, 255);
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
static void resetGame(lf2::Player& player, Enemy enemies[], GameSt& gameSt) {
    player.load(&g_dennis);
    player.x = 400.f;
    player.z = (float)Z_MIN;
    player.right = true;

    const float ex[]  = { 1600.f, 2200.f, 2800.f };
    const float ez[]  = { (float)Z_MIN, (float)(Z_MIN + Z_MAX) / 2, (float)Z_MAX };
    // Striking standoff beside the player (within reach): flank left / right /
    // left so the trio spreads but every enemy still closes in and connects.
    const float off[] = { -50.f, 50.f, -50.f };
    for (int i = 0; i < NUM_ENEMIES; i++) {
        enemies[i] = Enemy();
        enemies[i].load(&g_firen);
        enemies[i].a.x     = ex[i];
        enemies[i].a.z     = ez[i];
        enemies[i].aimOffset = off[i];
    }
    gameSt = GameSt::PLAYING;
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────
int main(int, char*[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    IMG_Init(IMG_INIT_PNG);

    // Character data — must be bundled in the VPK (see CMakeLists.txt).
    g_dennis = dat::load("app0:/data/dennis.dat");
    g_firen  = dat::load("app0:/data/firen.dat");

    SDL_Joystick* joy = nullptr;
    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

    SDL_Window*   win = SDL_CreateWindow(
        "LF2 Vita v0.7.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Textures tx;
    tx.load(ren);

    lf2::Player player;
    player.load(&g_dennis);
    Enemy  enemies[NUM_ENEMIES];
    GameSt gameSt        = GameSt::MENU;
    int    gameOverTimer = 0;
    bool   playerWon     = false;

    bool prevAtk = false, prevJmp = false, prevAny = false;
    int  lastSwingId = -1;   // player.swingId tracker → re-arms per-swing hit gates

    Uint32 nextTick = SDL_GetTicks();
    bool   running  = true;
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT) running = false;

        Uint32 now = SDL_GetTicks();
        while (now >= nextTick) {
            InputState raw = readInput(joy);

            bool atk      = raw.atk && !prevAtk;
            bool jmp      = raw.jmp && !prevJmp;
            bool anyPress = raw.any && !prevAny;
            prevAtk = raw.atk; prevJmp = raw.jmp; prevAny = raw.any;

            if (gameSt == GameSt::MENU) {
                if (anyPress) resetGame(player, enemies, gameSt);
            }
            else if (gameSt == GameSt::PLAYING) {
                // NOTE: spc is passed as LEVEL (held state) — the Player computes
                // its own edge and supports held-Square + direction/jump combos.
                player.tick(raw.L, raw.R, raw.U, raw.D, atk, jmp, raw.def, raw.spc);
                for (int i = 0; i < NUM_ENEMIES; i++)
                    enemies[i].tick(player.x, player.z);

                // New player swing (any attack start, INCLUDING chained punches)
                // clears the re-hit timers, re-arming every enemy immediately.
                if (player.swingId != lastSwingId) {
                    for (int i = 0; i < NUM_ENEMIES; i++) enemies[i].rehitTimer = 0;
                    lastSwingId = player.swingId;
                }

                // ── Player attack → enemy bodies ──────────────────────────────
                // Re-hit is timer-gated (itr vrest/arest), NOT once-per-swing:
                // multi-hit moves (many_foot flurry) land repeatedly, and the
                // knockback comes from the itr's own dvx (2 = nudge that keeps
                // the victim in the combo; 12 = the finisher's launch).
                HitInfo hi;
                if (actorAttack(player, hi)) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        Box ebody;
                        if (enemies[i].rehitTimer == 0 && enemies[i].alive() &&
                            fabsf(player.z - enemies[i].a.z) <= 12.f &&
                            actorBody(enemies[i].a, ebody) &&
                            boxOverlap(hi.box, ebody))
                        {
                            enemies[i].rehitTimer = hi.rest;
                            enemies[i].hitFlash   = 10;
                            float kb = (float)(hi.dvx > 0 ? hi.dvx : 1);
                            enemies[i].a.hit(hi.injury, player.right ? kb : -kb, hi.fall);
                        }
                    }
                }

                // ── Enemy attacks → player body ───────────────────────────────
                Box pBody;
                if (player.alive() && actorBody(player, pBody)) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        HitInfo ehi;
                        if (!enemies[i].hasHitPlayer && enemies[i].alive() &&
                            fabsf(player.z - enemies[i].a.z) <= 12.f &&
                            actorAttack(enemies[i].a, ehi) &&
                            boxOverlap(ehi.box, pBody))
                        {
                            enemies[i].hasHitPlayer = true;
                            float kb = (float)(ehi.dvx > 0 ? ehi.dvx : 1);
                            player.hit(ehi.injury, enemies[i].a.right ? kb : -kb, ehi.fall);
                        }
                    }
                }

                // Bodies never block movement (LF2 lets you pass through); the
                // per-enemy aim offset keeps the trio from stacking.

                bool allDead = true;
                for (int i = 0; i < NUM_ENEMIES; i++)
                    if (enemies[i].alive()) { allDead = false; break; }

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
            renderMenu(ren);
        } else {
            int camX = clampI((int)player.x - SCREEN_W / 2, 0, MAP_W - SCREEN_W);
            renderBackground(ren, tx, camX);
            renderCharacters(ren, tx, player, enemies, camX);
            renderHUD(ren, tx, player, enemies);
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
