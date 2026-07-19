#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <psp2/kernel/processmgr.h>
#include <cmath>

#include "engine/types.hpp"
#include "engine/render.hpp"
#include "engine/dat.hpp"
#include "engine/fighter.hpp"
#include "characters/player.hpp"   // lf2::Player — frame-driven, replaces Char
#include "characters/enemy.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Character data (loaded once from the original .dat, bundled in the VPK).
//  The player is now fully data-driven; enemies still use the hand-tuned Firen
//  until they are migrated to lf2::Player too.
// ─────────────────────────────────────────────────────────────────────────────
static dat::File g_dennis;

// ─────────────────────────────────────────────────────────────────────────────
//  Collision helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline bool isKnockdown(int dmg) { return dmg >= 25; }

static inline Box toBox(const lf2::WBox& w) {
    return { (int)w.x, (int)w.y, (int)w.w, (int)w.h };
}

// The player's first attacking itr (kind 0) for this frame, in world space.
static bool playerAttack(const lf2::Player& p, Box& out, int& injury) {
    bool found = false;
    p.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
        if (it.kind == 0 && !found) {
            out    = toBox(wb);
            injury = it.injury > 0 ? it.injury : 30;
            found  = true;
        }
    });
    return found;
}

// The player's first body box (kind 0) for this frame, in world space.
static bool playerBody(const lf2::Player& p, Box& out) {
    bool found = false;
    p.f.forEachBdy([&](const lf2::WBox& wb, const dat::Bdy&) {
        if (!found) { out = toBox(wb); found = true; }
    });
    return found;
}

static void applyHitToEnemy(Enemy& e, int dmg, float kbx) {
    e.hitFlash = 15;
    e.hp = clampI(e.hp - dmg, 0, e.maxHp);
    if (e.state == St::FALL || e.state == St::DEAD) return;
    e.enter(isKnockdown(dmg) ? St::HIT_KD : St::HIT, 0, kbx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input helpers (PS Vita joystick → bool directions / buttons)
// ─────────────────────────────────────────────────────────────────────────────
struct InputState { bool L, R, U, D, atk, jmp, any; };

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
    in.any = in.atk || in.jmp || in.L || in.R || in.U || in.D;
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
    // Player sheets, indexed by the .dat's declared file() order:
    //   0 → pics 0-69, 1 → pics 70-139, 2 → pics 140-209
    SDL_Texture* charSheet[3] = { nullptr, nullptr, nullptr };
    SDL_Texture* enemyTex = nullptr;
    SDL_Texture* shadow   = nullptr;

    void load(SDL_Renderer* r) {
        forests  = loadTex(r, "app0:/assets/forests.png",  false);
        forestm1 = loadTex(r, "app0:/assets/forestm1.png");
        forestm2 = loadTex(r, "app0:/assets/forestm2.png");
        forestm3 = loadTex(r, "app0:/assets/forestm3.png");
        forestm4 = loadTex(r, "app0:/assets/forestm4.png");
        foresett = loadTex(r, "app0:/assets/forestt.png");
        land1    = loadTex(r, "app0:/assets/land1.png",    false);
        land2    = loadTex(r, "app0:/assets/land2.png",    false);
        land4    = loadTex(r, "app0:/assets/land4.png",    false);
        charSheet[0] = loadTex(r, "app0:/assets/dennis_0.png");
        charSheet[1] = loadTex(r, "app0:/assets/dennis_1.png");
        charSheet[2] = loadTex(r, "app0:/assets/dennis_2.png");
        enemyTex     = loadTex(r, "app0:/assets/firen_0.png");
        shadow       = loadTex(r, "app0:/assets/s.png",    false);
    }
};

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
//  Render: characters sorted by Z (painter's algorithm)
// ─────────────────────────────────────────────────────────────────────────────
static void renderCharacters(SDL_Renderer* r, const Textures& tx,
                             const lf2::Player& player, Enemy enemies[], int camX)
{
    if (tx.shadow) {
        SDL_Rect ps = { (int)player.x - camX + 12, (int)player.z - 6, 56, 12 };
        SDL_RenderCopy(r, tx.shadow, nullptr, &ps);
        for (int i = 0; i < NUM_ENEMIES; i++) {
            SDL_Rect es = { (int)enemies[i].x - camX + 12, (int)enemies[i].z - 6, 56, 12 };
            SDL_RenderCopy(r, tx.shadow, nullptr, &es);
        }
    }

    struct Drawable { float z; int type; int idx; };
    Drawable dlist[NUM_ENEMIES + 1];
    dlist[0] = { player.z, 0, 0 };
    for (int i = 0; i < NUM_ENEMIES; i++) dlist[i + 1] = { enemies[i].z, 1, i };

    for (int i = 1; i < NUM_ENEMIES + 1; i++) {
        Drawable key = dlist[i]; int j = i - 1;
        while (j >= 0 && dlist[j].z > key.z) { dlist[j + 1] = dlist[j]; j--; }
        dlist[j + 1] = key;
    }

    for (int i = 0; i < NUM_ENEMIES + 1; i++) {
        if (dlist[i].type == 0) {
            // Player: map the current pic to its real sheet + local cell, then
            // blit from the .dat-declared sprite origin (fixes the old pic>=100
            // split and the missing dennis_2 sheet).
            int ord, loc;
            if (player.f.sheetLocal(ord, loc) && ord >= 0 && ord < 3 &&
                tx.charSheet[ord])
            {
                float dx, dy;
                player.f.drawOrigin(dx, dy);
                drawSprite(r, tx.charSheet[ord], loc,
                           (int)dx - camX, (int)dy, !player.right);
            }
        } else {
            int k = dlist[i].idx;
            bool flash = enemies[k].hitFlash > 0;
            drawSprite(r, tx.enemyTex, enemies[k].pic(),
                       (int)enemies[k].x - camX, enemies[k].spriteY(),
                       !enemies[k].right,
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
    drawSpriteAt(r, tx.charSheet[0], 0, 2, 2, PORT_W, PORT_H, false);
    drawHpBar(r, BAR_X, 10, player.hp(), player.maxHp(), 210, 40,  40);
    drawHpBar(r, BAR_X, 28,           0, player.maxHp(),  40, 110, 210);

    for (int i = 0; i < NUM_ENEMIES; i++) {
        int sx = (i + 1) * SW_SLOT;
        fillRect(r, sx + 2, 2, PORT_W, PORT_H, 18, 36, 90);
        drawSpriteAt(r, tx.enemyTex, 0, sx + 2, 2, PORT_W, PORT_H, true);
        drawHpBar(r, sx + BAR_X, 10, enemies[i].hp, enemies[i].maxHp, 210, 40,  40);
        drawHpBar(r, sx + BAR_X, 28,             0, enemies[i].maxHp,  40, 110, 210);
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

    const float ex[] = { 1600.f, 2200.f, 2800.f };
    const float ez[] = { (float)Z_MIN, (float)(Z_MIN + Z_MAX) / 2, (float)Z_MAX };
    for (int i = 0; i < NUM_ENEMIES; i++) {
        enemies[i]   = Enemy();
        enemies[i].x = ex[i];
        enemies[i].z = ez[i];
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

    SDL_Joystick* joy = nullptr;
    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

    SDL_Window*   win = SDL_CreateWindow(
        "LF2 Vita v0.6.0",
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
    bool prevPlayerAttacking = false;   // rising edge = new attack swing

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
                player.tick(raw.L, raw.R, raw.U, raw.D, atk, jmp);
                for (int i = 0; i < NUM_ENEMIES; i++)
                    enemies[i].tick(player.x, player.z);

                // New attack swing → let each enemy be hit once by it.
                bool attacking = (player.state() == lf2::ST_ATTACK);
                if (attacking && !prevPlayerAttacking)
                    for (int i = 0; i < NUM_ENEMIES; i++) enemies[i].hitByPlayer = false;
                prevPlayerAttacking = attacking;

                // ── Player attack → enemy body ────────────────────────────────
                Box atkBox; int atkInjury = 0;
                if (playerAttack(player, atkBox, atkInjury)) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        if (!enemies[i].hitByPlayer && enemies[i].alive() &&
                            fabsf(player.z - enemies[i].z) <= 12.f &&
                            boxOverlap(atkBox, enemies[i].bdy()))
                        {
                            enemies[i].hitByPlayer = true;
                            float kbx = player.right ? 30.f : -30.f;
                            applyHitToEnemy(enemies[i], atkInjury, kbx);
                        }
                    }
                }

                // ── Enemy attack → player body ────────────────────────────────
                Box pBody; bool haveBody = playerBody(player, pBody);
                if (haveBody && player.alive()) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        if (!enemies[i].atkHit &&
                            fabsf(player.z - enemies[i].z) <= 12.f &&
                            boxOverlap(enemies[i].itr(), pBody))
                        {
                            enemies[i].atkHit = true;
                            float kbx = enemies[i].right ? 30.f : -30.f;
                            player.hit(FIREN_INJURY, kbx, isKnockdown(FIREN_INJURY));
                        }
                    }
                }

                // ── Body separation ───────────────────────────────────────────
                Box pSep;
                bool playerSeparable = player.alive() &&
                    player.state() != lf2::ST_FALLING &&
                    player.state() != lf2::ST_LYING &&
                    playerBody(player, pSep);
                auto enemySeparable = [](St s) { return s != St::DEAD && s != St::FALL; };

                if (playerSeparable) {
                    for (int i = 0; i < NUM_ENEMIES; i++) {
                        if (enemySeparable(enemies[i].state)) {
                            separateX(player.x, player.z, pSep,
                                      enemies[i].x, enemies[i].z, FIREN_BDY);
                        }
                    }
                }
                for (int i = 0; i < NUM_ENEMIES; i++)
                for (int j = i + 1; j < NUM_ENEMIES; j++) {
                    if (enemySeparable(enemies[i].state) && enemySeparable(enemies[j].state))
                        separateX(enemies[i].x, enemies[i].z, FIREN_BDY,
                                  enemies[j].x, enemies[j].z, FIREN_BDY);
                }

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
