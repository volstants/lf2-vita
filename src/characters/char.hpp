#pragma once
#include "../engine/types.hpp"
#include "dennis.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Char — player-controlled character (Dennis)
//
//  Coordinate system:
//    x  : world X (pixels)
//    z  : ground depth (Z_MIN … Z_MAX, increases downward on screen)
//    h  : height above ground — negative while airborne, 0 when grounded
//    vy : vertical velocity (gravity accelerates positive)
//    vx : horizontal launch velocity (jumping / dash)
// ─────────────────────────────────────────────────────────────────────────────
struct Char {
    float x    = 400.f;
    float z    = Z_MIN;
    float h    = 0.f;
    float vy   = 0.f;
    float vx   = 0.f;

    bool right       = true;
    bool atkHit      = false;   // has this swing already landed?
    bool comboQueued = false;   // player pressed A during first punch
    bool newAttack   = false;   // set true on swing start to reset enemy hitByPlayer

    int  tapTimer = 0;          // double-tap run detection window (ticks)
    int  tapDir   = 0;          // +1 right, -1 left
    bool prevL = false, prevR = false;

    int  hp    = DENNIS_HP;
    int  maxHp = DENNIS_HP;

    St  state = St::IDLE;
    int si    = 0;              // index within current sequence
    int wt    = 5;              // ticks remaining on this frame

    // ── Accessors ─────────────────────────────────────────────────────────────
    const FrameData* seq()  const { return DENNIS_SEQS[(int)state]; }
    int  pic()              const { return seq()[si].pic; }
    bool grounded()         const { return h >= 0.f; }
    int  spriteY()          const { return (int)(z + h) - SH; }
    bool alive()            const { return state != St::DEAD; }

    // ── Hitbox accessors ──────────────────────────────────────────────────────
    Box bdy() const {
        return { (int)x + DENNIS_BDY.x,
                 spriteY() + DENNIS_BDY.y,
                 DENNIS_BDY.w, DENNIS_BDY.h };
    }

    Box itr() const {
        // Punch 1
        if (state == St::ATTACK && si == DENNIS_ITR_SI) {
            const Box& r = right ? DENNIS_ITR_R : DENNIS_ITR_L;
            return { (int)x + r.x, spriteY() + r.y, r.w, r.h };
        }
        // Punch 2 (combo)
        if (state == St::ATTACK2 && si == DENNIS_ITR2_SI) {
            const Box& r = right ? DENNIS_ITR2_R : DENNIS_ITR2_L;
            return { (int)x + r.x, spriteY() + r.y, r.w, r.h };
        }
        return { 0, 0, 0, 0 };
    }

    // ── State transitions ─────────────────────────────────────────────────────
    void enter(St s, int i = 0, float kbx = 0.f) {
        St prev = state;
        state = s; si = i; wt = seq()[i].wait;

        if (s == St::JUMP) {
            if (prev == St::RUN) {
                // Running jump → forward dash
                state = St::DASH; si = 0; wt = DENNIS_DASH_SEQ[0].wait;
                h  = -0.1f;
                vy = -10.f;
                vx = right ? 17.f : -17.f;
                return;
            }
            h  = -0.1f;
            vy = JUMP_VY;
            vx = (prev == St::WALK) ? (right ? WALK_SPEED : -WALK_SPEED) : 0.f;
        }
        if (s == St::ATTACK)  { atkHit = false; comboQueued = false; newAttack = true; }
        if (s == St::ATTACK2) { atkHit = false; comboQueued = false; newAttack = true; }
        if (s == St::HIT || s == St::HIT_KD) {
            x = clampF(x + kbx, 0.f, (float)(MAP_W - SW));
        }
        if (s == St::RUN) { tapTimer = 0; }
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
        // nx == -1 → sequence ended, pick next state
        switch (state) {
            case St::ATTACK:
                enter(comboQueued ? St::ATTACK2 : St::IDLE);
                break;
            case St::HIT_KD:
                // heavy hit → start falling
                enter(St::FALL);
                break;
            case St::FALL:
                enter(St::DEAD);
                break;
            default:
                enter(St::IDLE);
                break;
        }
    }

    // ── Per-tick update ───────────────────────────────────────────────────────
    void tick(bool L, bool R, bool U, bool D, bool atk, bool jmp) {
        if (state == St::DEAD) return;

        // Death / fall check — overrides all input
        if (hp <= 0 && state != St::FALL) { enter(St::FALL); return; }
        if (state == St::FALL)  { advanceAnim(); return; }
        if (state == St::HIT || state == St::HIT_KD) { advanceAnim(); return; }

        // ── Double-tap detection for RUN ──────────────────────────────────────
        bool newL = L && !prevL, newR = R && !prevR;
        prevL = L; prevR = R;
        if (tapTimer > 0) tapTimer--;
        if (state == St::IDLE || state == St::WALK) {
            if (newR) { if (tapDir == 1  && tapTimer > 0) enter(St::RUN); else { tapDir =  1; tapTimer = 10; } }
            if (newL) { if (tapDir == -1 && tapTimer > 0) enter(St::RUN); else { tapDir = -1; tapTimer = 10; } }
        }

        // ── Queue combo while punching ────────────────────────────────────────
        if (state == St::ATTACK && atk) comboQueued = true;

        // ── IDLE / WALK transitions ───────────────────────────────────────────
        if (state == St::IDLE || state == St::WALK) {
            if      (atk) { enter(St::ATTACK); }
            else if (jmp) { enter(St::JUMP);   }
            else {
                St want = (L || R || U || D) ? St::WALK : St::IDLE;
                if (state != want) enter(want);
            }
        }

        // ── RUN ───────────────────────────────────────────────────────────────
        if (state == St::RUN) {
            bool keepDir = right ? (R && !L) : (L && !R);
            if      (atk)     { enter(St::ATTACK); }
            else if (jmp)     { enter(St::JUMP);   }
            else if (!keepDir){ enter(St::IDLE);   }
            else {
                x += right ? DENNIS_RUN_SPEED : -DENNIS_RUN_SPEED;
                if (U) z -= DENNIS_RUN_SPEEDZ;
                if (D) z += DENNIS_RUN_SPEEDZ;
                x = clampF(x, 0.f, (float)(MAP_W - SW));
                z = clampF(z, (float)Z_MIN, (float)Z_MAX);
                advanceAnim();
                return;
            }
        }

        // ── Grounded movement (not attacking / not airborne) ──────────────────
        if (state != St::ATTACK && state != St::DASH && state != St::JUMP) {
            if (L) { x -= WALK_SPEED; right = false; }
            if (R) { x += WALK_SPEED; right = true;  }
            if (U) z -= WALK_SPEEDZ;
            if (D) z += WALK_SPEEDZ;
            x = clampF(x, 0.f, (float)(MAP_W - SW));
            z = clampF(z, (float)Z_MIN, (float)Z_MAX);
        }

        // ── DASH (running jump) ───────────────────────────────────────────────
        if (state == St::DASH) {
            vy += GRAVITY; h += vy;
            x = clampF(x + vx, 0.f, (float)(MAP_W - SW));
            if (U) z -= DENNIS_RUN_SPEEDZ;
            if (D) z += DENNIS_RUN_SPEEDZ;
            z = clampF(z, (float)Z_MIN, (float)Z_MAX);
            if (grounded()) { h = 0.f; vy = 0.f; vx = 0.f; enter(St::IDLE); return; }
            advanceAnim();
            return;
        }

        // ── JUMP ──────────────────────────────────────────────────────────────
        if (state == St::JUMP) {
            vy += GRAVITY; h += vy;
            x = clampF(x + vx, 0.f, (float)(MAP_W - SW));
            if (U) z -= 3.75f;
            if (D) z += 3.75f;
            z = clampF(z, (float)Z_MIN, (float)Z_MAX);
            if (grounded()) { h = 0.f; vy = 0.f; vx = 0.f; enter(St::IDLE); return; }
            // Pick jump frame by vertical velocity
            int w = (vy < -1.f) ? 0 : (vy < 1.f) ? 1 : 2;
            if (si != w) { si = w; wt = DENNIS_JUMP_SEQ[w].wait; }
            return;
        }

        advanceAnim();
    }
};
