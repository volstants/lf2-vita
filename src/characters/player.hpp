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
    constexpr int STANDING  = 0;   // 0-3
    constexpr int WALKING   = 5;   // 5-8
    constexpr int WALK_LAST = 8;
    constexpr int RUNNING   = 9;   // 9-11
    constexpr int RUN_LAST  = 11;
    constexpr int PUNCH       = 60; // basic attack
    constexpr int JUMP_ATTACK = 80; // 80-82 (state 3)
    constexpr int RUN_ATTACK  = 85; // running attack
    constexpr int DASH_ATTACK = 90; // 90-92 (state 15)
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

    // LF2 fall counter: a knockdown "budget" that each hit spends (by the itr's
    // `fall`) and that regenerates over time. Weak hits stagger in place; only
    // when the budget runs out does the actor get knocked down — so a rapid
    // combo floors you but spaced single hits don't.
    static constexpr int FALL_MAX = 60;
    int   fallValue = FALL_MAX;

    // Input edge / double-tap-to-run tracking.
    bool  prevL = false, prevR = false;
    int   tapDir = 0, tapTimer = 0;
    static constexpr int RUN_TAP_WINDOW = 9;   // ticks to land the 2nd tap (~0.3s)

    // Locomotion animation cursor (walking/running cycle their own frames; the
    // frame-graph `next` only handles idle/attacks).
    int   animTimer = 0;

    // Stats pulled from the character header (fall back to Dennis defaults).
    float walkSpeed  = 5.0f;
    float walkSpeedZ = 2.5f;
    float runSpeed   = 10.5f;
    float runSpeedZ  = 1.65f;
    int   walkRate   = 3;      // ticks per walking frame
    int   runRate    = 3;      // ticks per running frame
    float jumpVy     = -16.3f;
    float jumpDist   = 10.0f;
    float dashDist   = 18.0f;  // running-jump (dash) horizontal speed
    float dashVy     = -10.0f; // dash lift (lower/faster than a jump)

    // ── Setup ────────────────────────────────────────────────────────────────
    void load(const dat::File* d) {
        f.load(d);
        walkSpeed  = d->header.get("walking_speed",   5.0f);
        walkSpeedZ = d->header.get("walking_speedz",  2.5f);
        runSpeed   = d->header.get("running_speed",  10.5f);
        runSpeedZ  = d->header.get("running_speedz",  1.65f);
        walkRate   = (int)d->header.get("walking_frame_rate", 3.f);
        runRate    = (int)d->header.get("running_frame_rate", 3.f);
        jumpVy     = d->header.get("jump_height",    -16.3f);
        jumpDist   = d->header.get("jump_distance",   10.0f);
        dashDist   = d->header.get("dash_distance",   18.0f);
        dashVy     = d->header.get("dash_height",    -10.0f);
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
    void tick(bool L, bool R, bool U, bool D, bool atk, bool jmp, bool def = false) {
        // Input edge + double-tap bookkeeping (every tick).
        bool newL = L && !prevL, newR = R && !prevR;
        prevL = L; prevR = R;
        if (tapTimer > 0) --tapTimer;
        if (fallValue < FALL_MAX) ++fallValue;   // knockdown budget regenerates

        if (!grounded()) { airborneTick(U, D, atk); syncAnchor(); return; }

        int s = f.state();

        // Lying: dead → pinned; alive → animate up (230 → 219 → standing).
        if (s == ST_LYING) {
            if (f.hp > 0) { f.advance(); if (f.state() != ST_LYING) knockedDown = false; }
            syncAnchor();
            return;
        }

        // Guarding: hold the idle guard (110) while held; if a blocked hit bumped
        // us to the block-recoil (111), let it animate back to 110.
        if (s == ST_DEFEND) {
            if      (!def)                     f.setFrame(fid::STANDING); // dropped guard
            else if (f.frameId != fid::DEFEND) f.advance();              // recoil 111 → 110
            syncAnchor();
            return;
        }

        // Running has its own control + animation cycle.
        if (s == ST_RUNNING) { runTick(L, R, U, D, atk, jmp); syncAnchor(); return; }

        if (s == ST_STANDING || s == ST_WALKING) {
            if (def)      { f.setFrame(fid::DEFEND); }   // raise guard (blocks move/atk)
            else if (atk) { f.setFrame(fid::PUNCH); }
            else if (jmp) { startJump(L || R); }
            else {
                if (L) right = false;
                if (R) right = true;

                // Double-tap the facing direction within the window → run.
                if      (newR && tapDir ==  1 && tapTimer > 0) { enterRun(); }
                else if (newL && tapDir == -1 && tapTimer > 0) { enterRun(); }
                else {
                    if (newR) { tapDir =  1; tapTimer = RUN_TAP_WINDOW; }
                    if (newL) { tapDir = -1; tapTimer = RUN_TAP_WINDOW; }

                    bool moving = L || R || U || D;
                    if (moving) {
                        if (L) x -= walkSpeed;
                        if (R) x += walkSpeed;
                        if (U) z -= walkSpeedZ;
                        if (D) z += walkSpeedZ;
                        clampPos();
                        cycleAnim(fid::WALKING, fid::WALK_LAST, walkRate);  // 5→8 loop
                    } else {
                        if (s != ST_STANDING) f.setFrame(fid::STANDING);
                        f.advance();                                       // idle via next-graph
                    }
                }
            }
        }
        else {
            // attack / dash / defend / injured …: drift by the frame's own dv
            // and animate back to standing via the frame graph's next:999.
            x += f.vx;
            clampPos();
            f.advance();
        }

        syncAnchor();
    }

    // ── Airborne integration (jump + knockdown) ──────────────────────────────
    // Physics take over whenever off the ground, INDEPENDENT of the animation
    // state — jump frames can cycle to standing (next:999) mid-arc, so grounding
    // is decided by h, not by the frame's state, or the actor freezes floating.
    void airborneTick(bool U, bool D, bool atk) {
        vy += GRAVITY;
        h  += vy;
        x  += f.vx;
        if (!knockedDown) {                     // no air-steering while knocked down
            if (U) z -= walkSpeedZ * 0.75f;
            if (D) z += walkSpeedZ * 0.75f;
        }
        clampPos();
        if (grounded()) {                       // landed this tick
            h = 0.f; vy = 0.f; f.vx = 0.f;
            f.setFrame(knockedDown ? fid::LYING : fid::STANDING);
            return;
        }
        if (knockedDown) return;                // a knockdown holds its falling frame

        int s = f.state();
        // Already mid air-attack (jump_attack = state 3, dash_attack = state 15):
        // let its frames play out.
        if (s == ST_ATTACK || s == ST_SPECIAL) { f.advance(); return; }
        // Attack pressed in the air → jump attack, or dash attack from a dash.
        if (atk) {
            f.setFrame(s == ST_DASH ? fid::DASH_ATTACK : fid::JUMP_ATTACK);
            return;
        }
        // Otherwise pick the airborne pose by vertical velocity. Walking the
        // next-graph here would fall through to standing mid-air (the "jumps in a
        // standing pose" bug). Dash has its own 213/214 frames.
        if (s == ST_DASH) {
            int df = (vy < 0.f) ? fid::DASH : fid::DASH + 1;          // 213 rise / 214 fall
            if (f.frameId != df) f.setFrame(df);
        } else {
            int jf = (vy < -1.f) ? fid::JUMP : (vy < 1.f) ? fid::JUMP + 1 : fid::JUMP + 2;
            if (f.frameId != jf) f.setFrame(jf);
        }
    }

    // ── Running ──────────────────────────────────────────────────────────────
    void runTick(bool L, bool R, bool U, bool D, bool atk, bool jmp) {
        if (atk) { f.setFrame(fid::RUN_ATTACK); return; }  // running attack, not stand punch
        if (jmp) { startDash();                 return; }  // running jump = dash
        bool keepDir = right ? (R && !L) : (L && !R);
        if (!keepDir) { f.setFrame(fid::STANDING); return; }
        x += right ? runSpeed : -runSpeed;
        if (U) z -= runSpeedZ;
        if (D) z += runSpeedZ;
        clampPos();
        cycleAnim(fid::RUNNING, fid::RUN_LAST, runRate);   // 9→11 loop
    }

    // Cycle a contiguous frame range [first,last] at `rate` ticks per frame.
    void cycleAnim(int first, int last, int rate) {
        if (f.frameId < first || f.frameId > last) { f.setFrame(first); animTimer = 0; return; }
        if (++animTimer >= (rate > 0 ? rate : 1)) {
            animTimer = 0;
            int nx = f.frameId + 1;
            if (nx > last) nx = first;
            f.setFrame(nx);
        }
    }

    void enterRun() { f.setFrame(fid::RUNNING); animTimer = 0; tapTimer = 0; }

    // ── Sub-behaviours ───────────────────────────────────────────────────────
    // Launch only: sets the arc; gravity is integrated by the airborne branch of
    // tick() from the next frame on.
    void startJump(bool moving) {
        f.setFrame(fid::JUMP);                 // state 4
        h  = -0.1f;
        vy = jumpVy;
        f.vx = moving ? (right ? jumpDist : -jumpDist) : 0.f;
    }

    // Running jump: a low, fast forward leap (state 5, frames 213-214).
    void startDash() {
        f.setFrame(fid::DASH);                 // state 5
        h  = -0.1f;
        vy = dashVy;
        f.vx = right ? dashDist : -dashDist;
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
    int hit(int dmg, float kbx, int itrFall) {
        if (itrFall < 0) itrFall = 20;                // default fall for unset itr
        bool heavyBlow    = itrFall >= 60;            // guard-breaking / instant fall
        bool blockedFront = isDefending() &&
                            ((kbx < 0.f) == right);   // struck from the facing side
        if (blockedFront && !heavyBlow) {             // fully guarded — no damage
            f.setFrame(fid::DEFEND + 1);              // guard-recoil shake (frame 111)
            x = clampF(x + kbx * 0.15f, 0.f, (float)(MAP_W - SW));
            return 0;
        }

        int before = f.hp;
        int taken  = blockedFront ? dmg / 2 : dmg;    // chip through a broken guard
        f.hp = clampI(f.hp - taken, 0, f.maxHp);
        int lost = before - f.hp;

        if (blockedFront && heavyBlow) { f.setFrame(fid::BROKEN_DEF); return lost; }

        // Spend the fall budget. Knockdown only once it runs out (or on death);
        // otherwise stagger in place and keep taking hits.
        fallValue -= itrFall;
        if (fallValue <= 0 || f.hp <= 0) {            // knockdown
            fallValue   = FALL_MAX;
            knockedDown = true;
            f.setFrame(fid::FALLING);
            h = -0.1f; vy = -8.f;
            f.vx = (kbx < 0.f ? -6.f : 6.f);
        } else {                                      // stagger in place
            f.setFrame(fid::INJURED);
            x = clampF(x + kbx * 0.25f, 0.f, (float)(MAP_W - SW));   // small nudge only
        }
        return lost;
    }
};

} // namespace lf2
