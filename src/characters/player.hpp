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
    constexpr int PUNCH       = 60; // punch 1 (60-63)
    constexpr int PUNCH2      = 65; // punch 2 (65-68) — combo alternates 60/65
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
    // LF2 Falling Points: start 0, a hit ADDS the itr's `fall`, decays 1/frame.
    // FP > 40 → Dance of Pain (stunned in place); FP > 60 → knocked down, FP=0.
    // (Community-documented; z-band/anti-juggle cross-checked in OpenLF2.)
    static constexpr int FP_DOP = 40, FP_FALL = 60;
    int   fp = 0;

    // Input edge / double-tap-to-run tracking.
    bool  prevL = false, prevR = false, prevU = false, prevDn = false, prevDef = false,
          prevSpc = false;
    int   tapDir = 0, tapTimer = 0;
    static constexpr int RUN_TAP_WINDOW = 9;   // ticks to land the 2nd tap (~0.3s)

    // Special input: SQUARE arms it, a direction fires it. Both orders work —
    // hold a direction and tap Square (fires immediately), or tap Square then a
    // direction within the window. Attack alone NEVER fires specials.
    int   seqStage = 0;    // 0 idle · 1 = Square armed, waiting for a direction
    int   seqTimer = 0;
    int   wantSpecial = 0; // per-tick: 1 = F (sets facing) · 2 = Up · 3 = Down
    bool  wantRight = true;
    static constexpr int SEQ_WINDOW = 15;      // ~0.5 s to press the direction

    // Attack-swing counter: bumped every time a NEW swing starts (punch, chained
    // punch, run/air attack, special). main.cpp uses it to re-arm per-swing hit
    // gates — without it, chained punches after the first never connect.
    int   swingId = 0;
    bool  comboQueued = false;                 // atk pressed during a punch → chain at its end

    // Locomotion animation cursor (walking/running cycle their own frames; the
    // frame-graph `next` only handles idle/attacks).
    int   animTimer = 0;
    bool  comboNext = false;   // which punch the next basic attack throws (60/65)
    int   mpRegenAcc = 0;      // MP regen divider (1 MP every other tick)

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
    // boxes / draw origin are correct. player.x IS the anchor (LF2's objectX:
    // the frame's centerx point), and z + h is the foot line (centery row).
    // The Fighter does ALL centerx/centery math itself (drawOrigin/worldBox);
    // pre-transforming here would cancel it out and pin the CELL instead of the
    // anchor — which made frames with varying centerx (kicks: 20-42) jitter.
    void syncAnchor() {
        f.facingRight = right;
        f.x = x;
        f.y = z + h;
        f.z = z;        // depth — opoint spawns read this; without it balls fire at z=0
    }

    void clampPos() {
        // x is the anchor (~sprite middle), so clamp by half a cell each side.
        x = clampF(x, (float)(SW / 2), (float)(MAP_W - SW / 2));
        z = clampF(z, (float)Z_MIN, (float)Z_MAX);
    }

    // ── Per-tick update (run at 30 Hz) ───────────────────────────────────────
    void tick(bool L, bool R, bool U, bool D, bool atk, bool jmp,
              bool def = false, bool spc = false) {
        // Input edge + double-tap bookkeeping (every tick).
        bool newL = L && !prevL, newR = R && !prevR;
        bool newU = U && !prevU, newDn = D && !prevDn;
        prevL = L; prevR = R; prevU = U; prevDn = D;
        if (tapTimer > 0) --tapTimer;
        if (fp > 0) --fp;                        // falling points decay 1/frame
        if ((++mpRegenAcc & 1) == 0 && f.mp < f.maxMp) ++f.mp;   // ~15 MP/s regen
        if (f.removed) { f.removed = false; f.setFrame(fid::STANDING); }   // 1000-code safety

        // Special machine. `spc` is the Square LEVEL (held state); edges are
        // computed here. Three ways to fire, so human timing never drops one:
        //   1. Square pressed while a direction/jump is already held → fires now.
        //   2. Direction/Jump tapped while Square is HELD → fires (Smash-style).
        //   3. Square tapped alone → arms for SEQ_WINDOW; next dir/jump fires.
        // Square+Jump = the jump special (hit_Fj — c_foot), mirroring D>J.
        bool newSpc = spc && !prevSpc, relSpc = !spc && prevSpc; prevSpc = spc;
        wantSpecial = 0;
        if (seqTimer > 0) --seqTimer; else seqStage = 0;
        if (newSpc) {
            if      (U)      { wantSpecial = 2; }
            else if (D)      { wantSpecial = 3; }
            else if (L || R) { wantSpecial = 1; wantRight = R; }
            else if (jmp)    { wantSpecial = 4; }
            else             { seqStage = 1; seqTimer = SEQ_WINDOW; }  // arm, await dir/jump
        } else if (spc) {                       // Square HELD: any new press fires
            if      (newU)         { wantSpecial = 2; }
            else if (newDn)        { wantSpecial = 3; }
            else if (newL || newR) { wantSpecial = 1; wantRight = newR; }
            else if (jmp)          { wantSpecial = 4; }
        } else if (seqStage == 1) {             // armed by an earlier tap
            if      (newU)         { wantSpecial = 2; seqStage = 0; }
            else if (newDn)        { wantSpecial = 3; seqStage = 0; }
            else if (newL || newR) { wantSpecial = 1; wantRight = newR; seqStage = 0; }
            else if (jmp)          { wantSpecial = 4; seqStage = 0; }
        }
        // Square TAPPED alone (armed then released, no direction) → the neutral
        // special = the character's forward special (hit_Fa). Gives every fighter
        // its signature move on a single button.
        if (relSpc && seqStage == 1) { wantSpecial = 1; seqStage = 0; }
        bool jumpConsumed = (wantSpecial == 4);   // don't ALSO jump on a jump special

        if (!grounded()) { airborneTick(U, D, atk); syncAnchor(); return; }

        int s = f.state();

        // Lying: dead → pinned; alive → animate up (230 → 219 → standing).
        if (s == ST_LYING) {
            if (f.hp > 0) { f.advance(); if (f.state() != ST_LYING) knockedDown = false; }
            syncAnchor();
            return;
        }

        // Guarding: hold the idle guard (110) while held; if a blocked hit bumped
        // us to the block-recoil (111), let it animate back to 110. A command
        // sequence may complete while the guard is still up (D held, then dir+A).
        if (s == ST_DEFEND) {
            if (trySpecial()) { syncAnchor(); return; }
            if      (!def)                     f.setFrame(fid::STANDING); // dropped guard
            else if (f.frameId != fid::DEFEND) f.advance();              // recoil 111 → 110
            syncAnchor();
            return;
        }

        // Running has its own control + animation cycle.
        if (s == ST_RUNNING) { runTick(L, R, U, D, atk, jmp); syncAnchor(); return; }

        if (s == ST_STANDING || s == ST_WALKING) {
            if (L) right = false;
            if (R) right = true;
            if (trySpecial()) { }                        // Square + direction/jump
            else if (def) { f.setFrame(fid::DEFEND); }   // raise guard (blocks move/atk)
            else if (atk) { throwPunch(); }              // plain attack = punch, ALWAYS
            else if (jmp && !jumpConsumed) { startJump(L || R); }
            else {
                comboNext = false;                       // idle → next attack starts fresh
                comboQueued = false;

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
            // Attack frames: pressing attack again during a punch BUFFERS the
            // chain; the alternate punch (60 ↔ 65) fires when this one finishes
            // — LF2's rapid-press punch1/punch2 combo. Everything else (special
            // / injured …) drifts by the frame's dv and follows the next-graph.
            if (atk && inPunch()) comboQueued = true;

            // Generic in-move branches from the frame's own hit_* table: looping
            // specials (c_foot 284→287→284) expose their EXIT on hit_d/hit_j
            // (288) — pressing Defend or Jump bails out of the loop early.
            const dat::Frame* frNow = f.cur();
            if (frNow) {
                if      (def && frNow->hit_d > 0) { f.setFrame(frNow->hit_d); syncAnchor(); return; }
                else if (jmp && frNow->hit_j > 0) { f.setFrame(frNow->hit_j); syncAnchor(); return; }
                else if (atk && !inPunch() && frNow->hit_a > 0) {
                    f.setFrame(frNow->hit_a); ++swingId; syncAnchor(); return;
                }
            }

            bool wasPunch = inPunch();
            int  before   = f.frameId;
            x += f.vx;
            clampPos();
            f.advance();

            // MP-drain frames (mp < 0, e.g. c_foot's -17): entering one costs
            // |mp| — that's what breaks the .dat's intentional infinite loop.
            // Out of MP → exit via the frame's hit_d (288) or, failing that,
            // straight to standing. Without this the whirlwind slides forever.
            if (f.frameId != before) {
                const dat::Frame* fr = f.cur();
                if (fr && fr->mp < 0) {
                    if (f.mp + fr->mp < 0) {
                        f.setFrame(fr->hit_d > 0 ? fr->hit_d : fid::STANDING);
                    } else {
                        f.mp += fr->mp;
                    }
                }
            }

            if (wasPunch && comboQueued && !inPunch()) {   // punch just ended
                throwPunch();
                comboQueued = false;
            }
        }

        syncAnchor();
    }

    // ── Attacks ───────────────────────────────────────────────────────────────
    // Square specials, each direction trying its attack-slot then its jump-slot
    // so characters that put a move on the "j" slot (Louis's up = hit_Uj, etc.)
    // still fire. Neutral Square = forward. NOTE: projectile specials animate;
    // their object is emitted by the opoint system.
    bool trySpecial() {
        if (!wantSpecial) return false;
        const dat::Frame* fr = f.cur();
        if (!fr) { wantSpecial = 0; return false; }
        if (wantSpecial == 1) right = wantRight;   // F-special faces the pressed side
        auto pick = [](int a, int j) { return a > 0 ? a : j; };
        int id = (wantSpecial == 1) ? pick(fr->hit_Fa, fr->hit_Fj)
               : (wantSpecial == 2) ? pick(fr->hit_Ua, fr->hit_Uj)
               : (wantSpecial == 3) ? pick(fr->hit_Da, fr->hit_Dj)
               :                      pick(fr->hit_Fj, fr->hit_ja);  // Square+Jump
        wantSpecial = 0;                           // consumed either way
        if (id <= 0) return false;
        // MP gate: the move's entry frame declares its cost. Not enough → the
        // input just whiffs (like the original when the blue bar runs dry).
        const dat::Frame* tf = f.data ? f.data->frame(id) : nullptr;
        int cost = tf ? tf->mp : 0;
        if (cost > 0) {
            if (f.mp < cost) return false;
            f.mp -= cost;
        }
        f.vx = 0.f;                // clear stale momentum; the frame's own dvx
        f.setFrame(id);            // (blastpush=0) then decides movement
        ++swingId;
        return true;
    }
    void throwPunch() {
        f.setFrame(comboNext ? fid::PUNCH2 : fid::PUNCH);
        comboNext = !comboNext;
        ++swingId;
    }
    bool inPunch() const { return f.frameId >= fid::PUNCH && f.frameId <= fid::PUNCH2 + 3; }

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
            int ls = f.state();
            // Landing mid air-attack: let the swing finish on the ground instead
            // of snapping to standing (dash/jump attacks cut short otherwise).
            if (!knockedDown && (ls == ST_ATTACK || ls == ST_SPECIAL)) { f.advance(); return; }
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
            ++swingId;
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
        if (trySpecial()) return;                                     // Square works mid-run
        if (atk) { f.setFrame(fid::RUN_ATTACK); ++swingId; return; }  // running attack
        if (jmp) { startDash();                            return; }  // running jump = dash
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
            x += kbx * 0.15f; clampPos();
            return 0;
        }

        int before = f.hp;
        int taken  = blockedFront ? dmg / 2 : dmg;    // chip through a broken guard
        f.hp = clampI(f.hp - taken, 0, f.maxHp);
        int lost = before - f.hp;

        if (blockedFront && heavyBlow) { f.setFrame(fid::BROKEN_DEF); return lost; }

        // Falling Points: each hit adds its `fall`. Over 60 (or death) → knocked
        // down; otherwise stay up and keep taking hits (a flurry accumulates FP
        // until it crosses 60). `kbx` carries the itr's own dvx (facing-signed):
        // flurry hits (dvx 2) barely nudge — keeping the victim in the combo —
        // while a launcher (dvx 12/fall 70) crosses 60 at once and throws.
        fp += itrFall;
        if (fp > FP_FALL || f.hp <= 0) {              // knockdown / juggle
            fp = 0;
            bool juggle = knockedDown;                // already airborne = re-hit
            knockedDown = true;
            f.setFrame(fid::FALLING);
            h = juggle ? h : -0.1f;
            vy = juggle ? -6.f : -8.f;                // re-loft
            // Juggle hits keep the victim in place (re-loft only); the FIRST
            // launch carries the itr's horizontal knockback.
            float lv = juggle ? 0.f : std::fabs(kbx);
            if (!juggle && lv < 6.f) lv = 6.f;
            f.vx = (kbx < 0.f ? -lv : lv);
        } else {                                      // injured / Dance of Pain
            // FP>40 = the longer DoP stun; below, a brief flinch. Both use the
            // injured frames (220→221→standing); DoP's extra length is a refinement.
            f.setFrame(fid::INJURED);
            x += kbx; clampPos();                     // dvx-sized nudge (a few px)
        }
        return lost;
    }
};

} // namespace lf2
