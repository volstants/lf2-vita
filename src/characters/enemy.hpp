#pragma once
#include "../engine/types.hpp"
#include "firen.hpp"
#include "char.hpp"   // forward reference for Char (target)

// ─────────────────────────────────────────────────────────────────────────────
//  Enemy — Firen with simple pursuit AI
//
//  AI behaviour:
//   1. Face the player every tick.
//   2. If within attack range AND cooldown expired → enter ATTACK.
//   3. Otherwise walk toward player (X and Z).
//   4. After finishing an attack the cooldown resets so the enemy pauses
//      briefly before swinging again (prevents infinite stunlock).
// ─────────────────────────────────────────────────────────────────────────────
struct Enemy {
    float x    = 1800.f;
    float z    = Z_MIN;
    float h    = 0.f;
    float vy   = 0.f;

    bool right       = false;
    bool atkHit      = false;
    bool hitByPlayer = false;

    int  hp    = FIREN_HP;
    int  maxHp = FIREN_HP;

    int  hitFlash   = 0;    // ticks of red tint remaining after taking damage
    int  aiCooldown = 60;   // ticks before AI can attack again

    St  state = St::IDLE;
    int si    = 0;
    int wt    = 5;

    // ── Accessors ─────────────────────────────────────────────────────────────
    const FrameData* seq()  const { return FIREN_SEQS[(int)state]; }
    int  pic()              const { return seq()[si].pic; }
    int  spriteY()          const { return (int)(z + h) - SH; }
    bool alive()            const { return state != St::DEAD; }

    Box bdy() const {
        return { (int)x + FIREN_BDY.x,
                 spriteY() + FIREN_BDY.y,
                 FIREN_BDY.w, FIREN_BDY.h };
    }

    Box itr() const {
        if (state != St::ATTACK || si != FIREN_ITR_SI) return { 0, 0, 0, 0 };
        const Box& r = right ? FIREN_ITR_R : FIREN_ITR_L;
        return { (int)x + r.x, spriteY() + r.y, r.w, r.h };
    }

    // ── State transitions ─────────────────────────────────────────────────────
    void enter(St s, int i = 0, float kbx = 0.f) {
        St prev = state;
        state = s; si = i; wt = seq()[i].wait;

        if (s == St::ATTACK) { atkHit = false; }
        if (s == St::HIT || s == St::HIT_KD) {
            x = clampF(x + kbx, 0.f, (float)(MAP_W - SW));
        }
        // FIX: reset cooldown only when returning to IDLE after an attack,
        //      not every time IDLE is entered (prevents instant re-attack).
        if (s == St::IDLE && prev == St::ATTACK) {
            aiCooldown = 90;
        }
    }

    // ── Animation advance ─────────────────────────────────────────────────────
    void advanceAnim() {
        if (--wt > 0) return;

        int nx = seq()[si].next;
        if (nx >= 0) {
            si = nx;
            wt = seq()[si].wait;
            return;
        }
        // Sequence ended
        switch (state) {
            case St::HIT_KD: enter(St::FALL);  break;
            case St::FALL:   enter(St::DEAD);  break;
            default:         enter(St::IDLE);  break;
        }
    }

    // ── Per-tick AI update ────────────────────────────────────────────────────
    void tick(const Char& target) {
        if (state == St::DEAD) return;

        if (hitFlash > 0) hitFlash--;

        if (hp <= 0 && state != St::FALL) { enter(St::FALL); return; }
        if (state == St::FALL)  { advanceAnim(); return; }
        if (state == St::HIT || state == St::HIT_KD) { advanceAnim(); return; }

        // Always face the player
        right = (target.x > x);

        float dx = fabsf(target.x - x);
        float dz = fabsf(target.z - z);

        if (state == St::IDLE || state == St::WALK) {
            if (aiCooldown > 0) aiCooldown--;

            if (dx < 150.f && dz < 60.f && aiCooldown <= 0) {
                enter(St::ATTACK);
            } else {
                bool moving = false;
                if (dx > 60.f) { x += right ? WALK_SPEED : -WALK_SPEED; moving = true; }
                if (dz > 30.f) { z += (target.z > z) ? WALK_SPEEDZ : -WALK_SPEEDZ; moving = true; }
                x = clampF(x, 0.f, (float)(MAP_W - SW));
                z = clampF(z, (float)Z_MIN, (float)Z_MAX);

                St want = moving ? St::WALK : St::IDLE;
                if (state != want) enter(want);
            }
        }

        advanceAnim();
    }
};
