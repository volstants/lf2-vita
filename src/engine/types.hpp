#pragma once
#include <cmath>   // fabsf (separateX) — keeps this header SDL-free / host-buildable

// ── Screen / Map ──────────────────────────────────────────────────────────────
// Native Vita panel. (Tried rendering in LF2's 794-wide logical space via
// SDL_RenderSetLogicalSize so the bg.dat layers would cover exactly: it wrecked
// the HUD/menu/camera layout on device. The viewport stays 960 and the
// background tiles to fill it instead — see renderBackground.)
constexpr int   SCREEN_W   = 960;
constexpr int   SCREEN_H   = 544;
constexpr int   MAP_W      = 3200;
constexpr int   NUM_ENEMIES = 3;

// ── Sprite sheet ──────────────────────────────────────────────────────────────
constexpr int FRAME_W    = 80;
constexpr int FRAME_H    = 80;
constexpr int SHEET_COLS = 10;
constexpr int SW         = FRAME_W;
constexpr int SH         = FRAME_H;

// ── World physics ─────────────────────────────────────────────────────────────
constexpr int   Z_MIN      = 365;
constexpr int   Z_MAX      = 505;
constexpr float WALK_SPEED  = 5.0f;
constexpr float WALK_SPEEDZ = 2.5f;
constexpr float GRAVITY     = 1.7f;   // LF2's per-tick gravity (community-documented)
// Gravity for a WEAPON in flight (thrown / opoint-launched arrows & shuriken).
// FROM THE BINARY: lf2.exe's floating-point constant pool holds 1.7 together with
// its own fractions — 1.133333 (×2/3) @0x48368, 0.566667 (/3) @0x48350,
// **0.425 (/4) @0x48358** and 0.17 (/10) @0x48360. So the original really does run
// some objects on a reduced gravity, and /4 is the value that matches a thrown
// weapon's arc. (Found by scanning lf2.exe for IEEE-754 doubles: Ghidra's C output
// never shows float literals, it only references their address.)
// This replaced 0.45, which had been calibrated by eye against an F.LF screenshot
// and happened to land within 6% of the real constant.
constexpr float WEAPON_FLY_GRAVITY = 0.425f;   // = 1.7 / 4, lf2.exe @0x48358
// Ground friction applied to a grounded fighter's vx each tick, and the speed
// below which it snaps to zero (F.LF: ps.fric = 1, GC.min_speed = 1).
constexpr float FRICTION  = 1.0f;
constexpr float MIN_SPEED = 1.0f;
constexpr float JUMP_VY     = -16.3f;   // from davis.js
constexpr int   TICK_MS     = 33;   // 30 Hz logic — LF2's native tick. The frame
                                    // interpreter's wait/dv values assume it; at
                                    // 60 Hz every animation runs double speed.

// ── [VITA] PS Vita SDL2 button indices ────────────────────────────────────────
constexpr int     BTN_UP     = 8;
constexpr int     BTN_DOWN   = 6;
constexpr int     BTN_LEFT   = 7;
constexpr int     BTN_RIGHT  = 9;
constexpr int     BTN_ATTACK  = 2;  // Cross
constexpr int     BTN_JUMP    = 1;  // Circle
constexpr int     BTN_DEFEND  = 0;  // Triangle (confirmed on device)
constexpr int     BTN_SPECIAL = 3;  // Square — arms a special; direction fires it
constexpr int     BTN_START   = 11; // Start — TEST MODE: toggles enemy AI on/off
constexpr int     BTN_SELECT  = 10; // Select — AUDIT MODE: every fighter cycles
                                    // through its own hit_ slots automatically,
                                    // so specials can be verified without having
                                    // to input the combos by hand.
constexpr int     DEADZONE   = 8000;   // compared against SDL_JoystickGetAxis (Sint16)

// ── Hit detection ─────────────────────────────────────────────────────────────
struct Box { int x, y, w, h; };

inline bool boxOverlap(Box a, Box b) {
    return a.w > 0 && b.w > 0
        && a.x < b.x + b.w && a.x + a.w > b.x
        && a.y < b.y + b.h && a.y + a.h > b.y;
}

// Push two characters apart on X so they don't overlap
inline void separateX(float& x1, float z1, const Box& b1,
                      float& x2, float z2, const Box& b2)
{
    if (fabsf(z1 - z2) > 20.f) return;
    float l1 = x1 + b1.x, r1 = l1 + b1.w;
    float l2 = x2 + b2.x, r2 = l2 + b2.w;
    if (r1 <= l2 || r2 <= l1) return;
    float ov   = (r1 - l2 < r2 - l1) ? r1 - l2 : r2 - l1;
    float push = ov * 0.5f;
    float c1   = l1 + r1, c2 = l2 + r2;
    if (c1 <= c2) { x1 -= push; x2 += push; }
    else          { x1 += push; x2 -= push; }
    auto clamp = [](float v, float lo, float hi){ return v < lo ? lo : v > hi ? hi : v; };
    x1 = clamp(x1, 0.f, (float)(MAP_W - SW));
    x2 = clamp(x2, 0.f, (float)(MAP_W - SW));
}

inline int   clampI(int   v, int   lo, int   hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float clampF(float v, float lo, float hi)  { return v < lo ? lo : v > hi ? hi : v; }

// ── Animation frame ───────────────────────────────────────────────────────────
struct FrameData { int pic, wait, next; };

// ── Character states ──────────────────────────────────────────────────────────
// Indices must match the SEQS arrays in each character header
enum class St {
    IDLE    = 0,
    WALK    = 1,
    JUMP    = 2,
    ATTACK  = 3,
    FALL    = 4,
    DEAD    = 5,
    ATTACK2 = 6,
    HIT     = 7,    // light stagger
    HIT_KD  = 8,    // heavy knockdown → transitions to FALL
    RUN     = 9,
    DASH    = 10
};

// ── Game states ───────────────────────────────────────────────────────────────
enum class GameSt { MENU, PLAYING, GAMEOVER };
