#pragma once
#include <cmath>
#include "player.hpp"   // lf2::Player — the enemy IS a frame-driven actor

// ─────────────────────────────────────────────────────────────────────────────
//  Enemy — AI-driven actor.
//
//  Instead of a hand-written state machine, an enemy is a lf2::Player whose
//  inputs come from a simple pursuit AI rather than the joystick. It reads its
//  own .dat (firen.dat today, any character tomorrow), so animation, physics,
//  hitboxes and damage reactions are all data-driven and identical to the
//  player's. main.cpp renders it from its own sheets and runs the same
//  frame-box collision against it.
// ─────────────────────────────────────────────────────────────────────────────
struct Enemy {
    lf2::Player a;               // the actor (position, fighter, combat)

    int  hitFlash    = 0;        // red-tint ticks after taking damage
    int  aiCooldown  = 60;       // ticks before it may attack again
    float aimOffset  = 0.f;      // horizontal standoff so the trio doesn't stack

    // Hit gating. LF2 re-hit model: after connecting, an itr can't re-hit the
    // same victim until its vrest/arest ticks expire — that's what makes
    // multi-hit moves (many_foot's flurry) land several times while single
    // punches land once. A new player swing clears the timer immediately.
    int  rehitTimer  = 0;        // ticks until the player's attacks may hit again
    bool hasHitPlayer = false;   // already landed on the player this swing?
    bool wasAttacking = false;
    bool newSwing     = false;   // true only on the tick an attack starts

    void load(const dat::File* d) { a.load(d); a.right = false; }

    // Bridges for main.cpp.
    float x() const { return a.x; }
    float z() const { return a.z; }
    bool  alive() const { return a.alive(); }

    // ── Per-tick AI ───────────────────────────────────────────────────────────
    void tick(float tx, float tz) {
        if (hitFlash   > 0) hitFlash--;
        if (aiCooldown > 0) aiCooldown--;
        if (rehitTimer > 0) rehitTimer--;

        int s = a.state();
        bool reacting = !a.alive() ||
                        s == lf2::ST_INJURED || s == lf2::ST_FALLING ||
                        s == lf2::ST_LYING;

        // Actual striking reach (punch itr ≈ 55 px; z hit tolerance ≈ 12 px,
        // matching main.cpp's collision). Attacking outside this just whiffs.
        constexpr float REACH_X = 55.f, REACH_Z = 12.f;

        bool L=false, R=false, U=false, D=false, atk=false;
        if (!reacting && s != lf2::ST_ATTACK) {
            a.right = (tx > a.x);                  // always face the player
            float dxP = std::fabs(tx - a.x);       // distance to the PLAYER (for striking)
            float dz  = std::fabs(tz - a.z);

            if (dxP <= REACH_X && dz <= REACH_Z && aiCooldown <= 0) {
                atk = true; aiCooldown = 60;       // genuinely in range → strike
            } else {
                // Close the distance: line up on the player's z-row, then walk to
                // a striking standoff beside them (aimOffset spreads the trio, but
                // stays within reach so everyone actually pursues and connects).
                float target = tx + aimOffset;
                if (dz > 10.f)                       { if (tz > a.z)     D = true; else U = true; }
                if (std::fabs(target - a.x) > 8.f)   { if (target > a.x) R = true; else L = true; }
            }
        }
        a.tick(L, R, U, D, atk, false);

        bool atkNow = (a.state() == lf2::ST_ATTACK);
        newSwing    = atkNow && !wasAttacking;
        wasAttacking = atkNow;
        if (newSwing) hasHitPlayer = false;
    }
};
