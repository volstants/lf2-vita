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
    int  spCooldown  = 150;      // ticks before it may throw a special again
    uint32_t rng     = 0x4F6CDD1Du;  // per-enemy xorshift seed
    float aimOffset  = 0.f;      // horizontal standoff so the trio doesn't stack

    int nextRand() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (int)(rng & 0x7fff); }

    // Hit gating. LF2 re-hit model: after connecting, an itr can't re-hit the
    // same victim until its vrest/arest ticks expire — that's what makes
    // multi-hit moves (many_foot's flurry) land several times while single
    // punches land once. A new player swing clears the timer immediately.
    int  rehitTimer  = 0;        // ticks until the player's attacks may hit again
    bool frozen      = false;    // TEST MODE: stand idle (still takes hits/reacts);
                                 // toggled by Start in main.cpp
    bool hasHitPlayer = false;   // already landed on the player this swing?
    bool wasAttacking = false;
    int  lastSwingId  = -1;      // mirrors a.swingId; a change means a new swing
    bool newSwing     = false;   // true only on the tick an attack starts

    void load(const dat::File* d) {
        a.load(d); a.right = false;
        static uint32_t seed = 0x1234;      // distinct per-enemy RNG so they desync
        rng ^= (seed += 0x9E3779B9u);
    }

    // Bridges for main.cpp.
    float x() const { return a.x; }
    float z() const { return a.z; }
    bool  alive() const { return a.alive(); }

    // ── Per-tick AI ───────────────────────────────────────────────────────────
    void tick(float tx, float tz) {
        if (hitFlash   > 0) hitFlash--;
        if (aiCooldown > 0) aiCooldown--;
        if (spCooldown > 0) spCooldown--;
        if (rehitTimer > 0) rehitTimer--;

        int s = a.state();
        bool reacting = !a.alive() ||
                        s == lf2::ST_INJURED || s == lf2::ST_FALLING ||
                        s == lf2::ST_LYING ||
                        // Burning (18) and frozen (13) are reaction states the
                        // victim does not control either — an enemy on fire that
                        // kept walking and punching was the AI ignoring them.
                        s == lf2::ST_BURNING || s == lf2::ST_ICE;

        constexpr float REACH_X = 55.f, REACH_Z = 12.f;

        bool L=false, R=false, U=false, D=false, atk=false, spc=false;
        if (!reacting && !frozen && s != lf2::ST_ATTACK && s != lf2::ST_SPECIAL) {
            a.right = (tx > a.x);                  // always face the player
            float dxP = std::fabs(tx - a.x);
            float dz  = std::fabs(tz - a.z);

            // On the player's z-row and cooled down → occasionally throw the
            // forward special (spawns the character's own projectile, costs MP).
            bool onRow = dz <= REACH_Z;
            if (onRow && spCooldown <= 0 && a.f.mp >= 100 && nextRand() < 4000) {
                spc = true; (tx > a.x) ? (R = true) : (L = true);
                spCooldown = 150 + nextRand() % 120;
            } else if (dxP <= REACH_X && dz <= REACH_Z && aiCooldown <= 0) {
                atk = true; aiCooldown = 60;       // in melee range → strike
            } else {
                float target = tx + aimOffset;
                if (dz > 10.f)                       { if (tz > a.z)     D = true; else U = true; }
                if (std::fabs(target - a.x) > 8.f)   { if (target > a.x) R = true; else L = true; }
            }
        }
        a.tick(L, R, U, D, atk, false, false, spc);

        // A new swing is signalled by the actor's swingId, NOT by entering
        // ST_ATTACK: specials run in ST_SPECIAL (15), so keying off state 3 meant
        // hasHitPlayer was never cleared between specials — the first one landed
        // and every one after it passed through the player doing nothing.
        newSwing     = (a.swingId != lastSwingId);
        lastSwingId  = a.swingId;
        wasAttacking = (a.state() == lf2::ST_ATTACK);
        if (newSwing) hasHitPlayer = false;
    }
};
