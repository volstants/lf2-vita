#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  player.hpp — LF2 player controller (frame-driven, replaces the Char/St model)
//
//  Owns the world position (x, ground-depth z, height-above-ground h) and drives
//  a lf2::Fighter that supplies animation, sprites and hitboxes from the real
//  .dat frames. Behaviour is keyed on the CURRENT frame's `state` field plus the
//  handful of canonical LF2 entry frame-ids (shared by every character file), so
//  no per-character code is needed — point it at any character .dat.
//
//  Pure C++ (types.hpp is now SDL-free), so it is unit-tested on the host.
//  This is a first playable cut: standing / walking / punch / jump. Running
//  (double-tap), dash, defend, catch and hit-reactions are deliberately deferred
//  to a later pass and marked TODO.
// ─────────────────────────────────────────────────────────────────────────────
#include "../engine/types.hpp"
#include "../engine/fighter.hpp"

namespace lf2 {

// Canonical LF2 frame ids — a 20-year-old community convention every character
// .dat obeys (verified against dennis.dat via datdump).
namespace fid {
    constexpr int STANDING = 0;    // 0-3
    constexpr int WALKING  = 5;    // 5-8
    constexpr int RUNNING  = 9;    // 9-11
    constexpr int PUNCH    = 60;   // basic attack
    constexpr int JUMP     = 210;  // 210-212
    constexpr int DASH     = 213;  // 213-217
    constexpr int DEFEND   = 110;  // 110-111
    constexpr int BROKEN_DEF = 112;// 112-114
    constexpr int INJURED  = 220;  // 220-221 light stagger → standing (999)
    constexpr int FALLING  = 180;  // 180-191 knockdown, engine picks by velocity
    constexpr int LYING    = 230;  // 230-231, next:219 (auto get-up) if alive
}

struct Player {
    Fighter f;

    // World model (unchanged from the previous engine).
    float x = 400.f;
    float z = (float)Z_MIN;
    float h = 0.f;            // height above ground (<0 = airborne)
    float vy = 0.f;
    bool  right = true;
    bool  knockedDown = false; // in a heavy-hit fall→lie sequence

    // Stats pulled from the character header (fall back to Dennis defaults).
    float walkSpeed  = 5.0f;
    float walkSpeedZ = 2.5f;
    float jumpVy     = -16.3f;
    float jumpDist   = 10.0f;

    // ── Setup ────────────────────────────────────────────────────────────────
    void load(const dat::File* d) {
        f.load(d);
        walkSpeed  = d->header.get("walking_speed",   5.0f);
        walkSpeedZ = d->header.get("walking_speedz",  2.5f);
        jumpVy     = d->header.get("jump_height",    -16.3f);
        jumpDist   = d->header.get("jump_distance",   10.0f);
        f.setFrame(fid::STANDING, /*applyDv=*/false);
        syncAnchor();
    }

    // ── Read-through accessors (render / HUD / collision) ────────────────────
    int  hp()    const { return f.hp; }
    int  maxHp() const { return f.maxHp; }
    int  state() const { return f.state(); }
    int  pic()   const { return f.pic(); }
    bool grounded()    const { return h >= 0.f; }
    bool isDefending() const { return f.state() == ST_DEFEND; }
    // Dead only once knocked down AND out of HP — while falling with 0 HP the
    // actor is still "dying", not yet lying.
    bool dead()  const { return f.hp <= 0 && f.state() == ST_LYING; }
    bool alive() const { return !dead(); }

    // Keep the Fighter's anchor aligned with the world model each tick so its
    // boxes / draw origin are correct. Sprite-left = x, foot line = z + h;
    // the Fighter anchor is the sprite's (centerx, centery) point.
    void syncAnchor() {
        const dat::Frame* fr = f.cur();
        int cx = fr ? fr->centerx : 0, cy = fr ? fr->centery : 0;
        f.facingRight = right;
        f.x = x + cx;
        f.y = (z + h - (float)SH) + cy;
    }

    void clampPos() {
        x = clampF(x, 0.f, (float)(MAP_W - SW));
        z = clampF(z, (float)Z_MIN, (float)Z_MAX);
    }

    // ── Per-tick update (run at 30 Hz) ───────────────────────────────────────
    void tick(bool L, bool R, bool U, bool D, bool atk, bool jmp) {
        // Airborne physics take over whenever off the ground, INDEPENDENT of the
        // animation state — jump frames can cycle to standing (next:999) mid-arc,
        // so grounding must be decided by h, not by the frame's state, or the
        // actor freezes floating.
        if (!grounded()) {
            vy += GRAVITY;
            h  += vy;
            x  += f.vx;
            if (!knockedDown) {                 // no air-steering while knocked down
                if (U) z -= walkSpeedZ * 0.75f;
                if (D) z += walkSpeedZ * 0.75f;
            }
            clampPos();
            if (grounded()) {                   // landed this tick
                h = 0.f; vy = 0.f; f.vx = 0.f;
                f.setFrame(knockedDown ? fid::LYING : fid::STANDING);
            } else if (!knockedDown) {
                f.advance();                    // jumps animate; a knockdown holds its frame
            }
            syncAnchor();
            return;
        }

        int s = f.state();

        // Lying: dead → pinned; alive → let it animate up (230 → 219 → standing).
        if (s == ST_LYING) {
            if (f.hp > 0) { f.advance(); if (f.state() != ST_LYING) knockedDown = false; }
            syncAnchor();
            return;
        }

        if (s == ST_STANDING || s == ST_WALKING) {
            if (atk)      { f.setFrame(fid::PUNCH); }        // → state 3
            else if (jmp) { startJump(L || R); }            // → state 4 (h<0 next tick)
            else {
                if (L) right = false;
                if (R) right = true;
                bool moving = L || R || U || D;
                if (moving) {
                    if (L) x -= walkSpeed;
                    if (R) x += walkSpeed;
                    if (U) z -= walkSpeedZ;
                    if (D) z += walkSpeedZ;
                    clampPos();
                    if (s != ST_WALKING) f.setFrame(fid::WALKING);
                    f.advance();
                } else {
                    if (s != ST_STANDING) f.setFrame(fid::STANDING);
                    f.advance();
                }
            }
        }
        else {
            // attack / dash / defend / injured …: drift by the frame's own dv
            // (set on entry, carried by the KEEP rule) and animate back to
            // standing via the frame graph's next:999.
            x += f.vx;
            clampPos();
            f.advance();
        }

        syncAnchor();
    }

    // ── Sub-behaviours ───────────────────────────────────────────────────────
    // Launch only: sets the arc; gravity is integrated by the airborne branch of
    // tick() from the next frame on.
    void startJump(bool moving) {
        f.setFrame(fid::JUMP);                 // state 4
        h  = -0.1f;
        vy = jumpVy;
        f.vx = moving ? (right ? jumpDist : -jumpDist) : 0.f;
    }

    // Raise a guard (state 7). The caller decides when to hold it up; hit()
    // below honours it for frontal attacks.
    void defend() {
        if (grounded() && (f.state() == ST_STANDING || f.state() == ST_WALKING))
            f.setFrame(fid::DEFEND);
    }

    // Take a hit. `kbx` is the horizontal knockback (already signed toward the
    // attacker's facing); `heavy` requests a knockdown. Returns the HP actually
    // lost (0 if fully blocked).
    //  • Blocking (front defend): light hits are absorbed; heavy hits break the
    //    guard (broken_defend) and still chip HP.
    //  • Light hit  → injured stagger (220 → 221 → standing).
    //  • Heavy/fatal → knockdown: launch into falling (180), land into lying;
    //    if HP hit 0 the actor stays down (dead), otherwise it gets back up.
    int hit(int dmg, float kbx, bool heavy) {
        bool blockedFront = isDefending() &&
                            ((kbx < 0.f) == right);   // struck from the facing side
        if (blockedFront && !heavy) return 0;         // fully guarded

        int before = f.hp;
        int taken  = blockedFront ? dmg / 2 : dmg;    // chip through a broken guard
        f.hp = clampI(f.hp - taken, 0, f.maxHp);
        int lost = before - f.hp;

        if (blockedFront && heavy) { f.setFrame(fid::BROKEN_DEF); return lost; }

        if (heavy || f.hp <= 0) {                     // knockdown
            knockedDown = true;
            f.setFrame(fid::FALLING);
            h = -0.1f; vy = -8.f;
            f.vx = (kbx < 0.f ? -6.f : 6.f);
        } else {                                      // light stagger
            f.setFrame(fid::INJURED);
            x = clampF(x + kbx, 0.f, (float)(MAP_W - SW));
        }
        return lost;
    }
};

} // namespace lf2
