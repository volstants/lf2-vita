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
    // Weapon frames (F.LF character.js, confirmed in dennis.dat):
    //   pick up  -> 115 light / 116 heavy   (picking_light / picking_heavy)
    //   attack   -> 20 or 25 (normal_weapon_atck, the original picks at random)
    //   throw    -> 45 light / 50 heavy     (light_/heavy_weapon_thw)
    constexpr int WEAPON_ATTACK  = 20, WEAPON_ATTACK2 = 25;
    constexpr int PICK_LIGHT     = 115, PICK_HEAVY    = 116;
    constexpr int THROW_LIGHT    = 45,  THROW_HEAVY   = 50;
    // Carrying a HEAVY object: overhead two-hand stance (not the recessed hold).
    constexpr int HEAVY_WALK = 12, HEAVY_WALK_LAST = 15;   // idle + walk (state 1)
    constexpr int HEAVY_RUN  = 16, HEAVY_RUN_LAST  = 18;   // run (state 2)
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
    // Hit reaction is tiered by falling points (F.LF character.js:1676-1686):
    // 220 (fp<=20) · 222 (21-30) · 224 (31-40) · 226 (41-60) = Dance of Pain,
    // state 16, the long stun and the only grabbable state.
    constexpr int DANCE_OF_PAIN = 226;
    // Hit-effect victim reactions (itr `effect`), canonical in every character:
    //   200 → 201 (state 13, wait 90 = 3 s frozen) → 202 → 182 falling
    //   203 ↔ 204 / 205 ↔ 206 (state 18) burning; loops, exit is external
    constexpr int ICE      = 200;
    constexpr int BURNING  = 203;  // 203↔204: queimando em pé
    constexpr int BURNING_AIR = 205; // 205↔206: a mesma queimadura caindo
                                     // (lf2.exe @0x40e8c5 troca 203/204 → 205
                                     //  assim que a velocidade vertical passa
                                     //  de 1.0; ver Player::tickInner)
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
    static constexpr int FP_DOP = 40, FP_FALL = 60;   // DoP / KO thresholds
    int   fp = 0, fpAcc = 0;

    // Input edge / double-tap-to-run tracking.
    bool  prevL = false, prevR = false, prevU = false, prevDn = false, prevDef = false,
          prevSpc = false;
    int   tapDir = 0, tapTimer = 0;
    static constexpr int RUN_TAP_WINDOW = 9;   // ticks to land the 2nd tap (~0.3s)

    // Special input, two paths:
    //  • SQUARE = shortcut for the HORIZONTAL (forward) special only: Square → hit_Fa,
    //    Square+Jump → hit_Fj. Up/Down on Square are ignored on purpose.
    //  • Faithful command: Defend → direction → A/J (all 6 slots) — how the
    //    original inputs D+dir+A / D+dir+J, incl. the vertical specials.
    int   wantSlot = 0;    // per-tick: frame hit_ id to fire
    int   seqStage = 0;    // 0 idle · 1 = Defend pressed · 2 = Defend+dir latched
    int   seqDir   = 0;    // 0 forward · 1 up · 2 down
    int   seqTimer = 0;
    static constexpr int SEQ_WINDOW = 15;   // ~0.5 s between the command's keys

    // Attack-swing counter: bumped every time a NEW swing starts (punch, chained
    // punch, run/air attack, special). main.cpp uses it to re-arm per-swing hit
    // gates — without it, chained punches after the first never connect.
    int   swingId = 0;

    // `next: 1280` (Rudolf's disappear): hidden AND untouchable while this runs
    // down. F.LF counts up to 150 TU with effect.super set, hiding sprite and
    // shadow, blinking the shadow over the last 30 before the body returns.
    static constexpr int VANISH_TICKS = 150;
    static constexpr int VANISH_BLINK = 30;    // final ticks: shadow blinks back
    int   vanish = 0;
    // Fire (itr effect 2/20/21/22/23): F.LF locks frame 203 for 36 TU, then the
    // burning victim collapses. Ice needs no timer — frame 201's wait 90 is the
    // 3 s freeze and the chain falls out on its own.
    static constexpr int BURN_TICKS = 36;
    int   burn = 0;
    // Fire and ice both make you DROP a held weapon (F.LF drop_weapon in the
    // effect switch) — except effect 20, the "weak fire" burn. main.cpp owns the
    // weapon slot, so it consumes this flag.
    bool  dropWeaponReq = false;
    bool  hidden()       const { return vanish > 0; }
    bool  untouchable()  const { return vanish > 0; }
    bool  shadowBlink()  const { return vanish > 0 && vanish <= VANISH_BLINK; }
    bool  comboQueued = false;                 // atk pressed during a punch → chain at its end
    int   heldWeapon  = -1;    // object-pool index of a held weapon, or -1
    bool  heavyWeapon = false; // the held weapon is type 2 (stone/box)

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
    // Carrying a heavy object slows you down — the .dat ships its own set
    // (dennis: 3.7/1.85 walking, 6.2/1.0 running vs 5.0/2.5 and 10.5/1.65).
    float heavyWalkSpeed  = 3.7f;
    float heavyWalkSpeedZ = 1.85f;
    float heavyRunSpeed   = 6.2f;
    float heavyRunSpeedZ  = 1.0f;
    bool  carryingHeavy() const { return heldWeapon >= 0 && heavyWeapon; }
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
        heavyWalkSpeed  = d->header.get("heavy_walking_speed",  3.7f);
        heavyWalkSpeedZ = d->header.get("heavy_walking_speedz", 1.85f);
        heavyRunSpeed   = d->header.get("heavy_running_speed",  6.2f);
        heavyRunSpeedZ  = d->header.get("heavy_running_speedz", 1.0f);
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
    // On the ground means BOTH resting on it and not moving off it. Testing h
    // alone made a frame's own dvy unusable: the drain sets vy negative while h
    // is still exactly 0, and with `h >= 0` the airborne branch never ran, so the
    // lift was frozen in place instead of integrated. This is LF2's own airborne
    // test — F.LF character.js:1668 `$.ps.y < 0 || $.ps.vy < 0`.
    bool grounded()    const { return h >= 0.f && vy >= 0.f; }
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

    // ── Frame dvy → world model ──────────────────────────────────────────────
    // Pull in whatever dvy the frame graph applied since the last call. Without
    // this the character data's dvy is dead: Fighter::applyDvy writes it to
    // Fighter::vy, which only free-flying objects integrate, while a character's
    // height runs entirely on Player::vy. Census of the 24 character files: 33
    // frames carry a non-zero dvy, chief among them frame 202 — the last ice
    // frame, dvy -3 in EVERY file — plus davis 290/300, john 290, deep 266/268/
    // 270, rudolf 274, woody 253 and jack 301/303.
    // 550 (DV_STOP) zeroes the velocity; any other value ADDS to it (F.LF
    // frame_force(): dvy accumulates where dvx/dvz assign).
    void drainFrameDvy() {
        if      (f.dvyStop)            vy = 0.f;
        else if (f.dvyPending != 0.f)  vy += f.dvyPending;
        f.dvyPending = 0.f; f.dvyStop = false; f.vy = 0.f;
    }

    // The engine's OWN launches (jump, dash, knockdown) assign the vertical
    // velocity outright, exactly as LF2 applies jump_height — so the entry
    // frame's dvy must not be added on top of them. Discard whatever the
    // setFrame() that precedes the launch staged.
    void discardFrameDvy() { f.dvyPending = 0.f; f.dvyStop = false; f.vy = 0.f; }
    void launch(float v) { h = -0.1f; vy = v; discardFrameDvy(); }

    // ── Per-tick update (run at 30 Hz) ───────────────────────────────────────
    // Thin wrapper so the frame's dvy is collected on EVERY path. tickInner has
    // a dozen early returns and frames are also entered from outside it (hit(),
    // which main.cpp calls between ticks), so draining at one point inside the
    // body would miss most of them. Drain on the way in (frames entered since
    // the last tick) and on the way out (frames entered by this tick).
    void tick(bool L, bool R, bool U, bool D, bool atk, bool jmp,
              bool def = false, bool spc = false) {
        drainFrameDvy();
        tickInner(L, R, U, D, atk, jmp, def, spc);
        drainFrameDvy();
    }

    void tickInner(bool L, bool R, bool U, bool D, bool atk, bool jmp,
                   bool def, bool spc) {
        // Input edge + double-tap bookkeeping (every tick).
        bool newL = L && !prevL, newR = R && !prevR;   // run double-tap edges
        bool newU = U && !prevU, newDn = D && !prevDn, newSpc = spc && !prevSpc;
        prevL = L; prevR = R; prevU = U; prevDn = D;
        if (tapTimer > 0) --tapTimer;
        // F.LF global.js: recover.fall = -0.45 per TU (not 1). Accumulated in
        // hundredths so the engine stays integer-only.
        if (fp > 0) { fpAcc += 45; while (fpAcc >= 100 && fp > 0) { fpAcc -= 100; --fp; } }
        if ((++mpRegenAcc & 1) == 0 && f.mp < f.maxMp) ++f.mp;   // ~15 MP/s regen
        if (f.removed) { f.removed = false; f.setFrame(fid::STANDING); }   // 1000-code safety
        if (f.vanishReq) { f.vanishReq = false; vanish = VANISH_TICKS; }    // next: 1280
        if (f.flipReq)   { f.flipReq = false; right = !right; }              // next < 0
        if (vanish > 0) --vanish;
        if (burn > 0 && --burn == 0 && f.state() == ST_BURNING) {
            // Enter the tumble; the promotion below turns it into a real knockdown
            // (setting knockedDown here would suppress that and freeze frame 180).
            // Clearing the flag first is what ARMS that promotion: a victim who
            // was already knockedDown when the fire caught him kept the flag, the
            // promotion was skipped, and he stood upright inside the falling frame
            // (180, `next: 0` = hold) forever instead of collapsing.
            knockedDown = false;
            f.setFrame(fid::FALLING);
        }

        // Queimando E CAINDO → os frames 205/206, a pose de fogo na horizontal.
        // SOURCE: lf2.exe 0x0040e893-0x0040e8c5 (dentro de FUN_0040e490, o update
        // por tick do objeto). Literalmente:
        //     mov  0x70(%esi),%eax            ; frame_id
        //     cmpl $0x12,0x7ac(%edx,%ecx,1)   ; frames[frame_id].state == 18 ?
        //     cmp  $0xcd,%eax                 ; frame_id < 205 ?
        //     fcompl 0x48(%esi)               ; 1.0 vs object->vy
        //     movl $0xcd,0x70(%esi)           ; frame_id = 205
        // É a prova de que no original o state 18 SOBREVIVE ao voo — ele não é
        // trocado por pose de pulo, ele ganha um par de frames próprio. 203↔204
        // é a queimadura em pé; 205↔206 é a mesma queimadura caindo.
        if (f.state() == ST_BURNING && f.frameId < fid::BURNING_AIR && vy > 1.f)
            f.setFrame(fid::BURNING_AIR);

        // GROUND FRICTION (F.LF mechanics.dynamics: ps.fric = 1 per tick while
        // ps.y === 0, snapped to 0 below GC.min_speed = 1).
        // Without it a leftover f.vx never died: `dvx: 0` means KEEP, and whole
        // attack chains are dvx 0 (Rudolf's shuriken frames 62/66/69/288 are all
        // 0), so any residual velocity — a knockback, the end of a run, a dash —
        // kept sliding the fighter backwards for every frame of the attack.
        if (grounded()) {
            if (f.vx > 0.f) f.vx -= FRICTION; else if (f.vx < 0.f) f.vx += FRICTION;
            if (f.vx > -MIN_SPEED && f.vx < MIN_SPEED) f.vx = 0.f;
        }

        prevSpc = spc;
        bool newDef = def && !prevDef; prevDef = def;
        wantSlot = 0;
        bool atkConsumed = false, jumpConsumed = false;
        const dat::Frame* fr = f.cur();

        // (1) SQUARE — forward special only (the horizontal D+>+A / D+>+J moves).
        if (spc && fr) {
            if (L) right = false;
            if (R) right = true;
            if (jmp) { wantSlot = fr->hit_Fj; jumpConsumed = true; }
            else     { wantSlot = fr->hit_Fa; if (atk) atkConsumed = true; }
            (void)newSpc;
        }

        // (2) Faithful command: Defend → direction → A/J → the frame's hit_ slot.
        if (seqTimer > 0) --seqTimer; else seqStage = 0;
        if (newDef) { seqStage = 1; seqDir = 0; seqTimer = SEQ_WINDOW; }
        // Direction must be pressed while Defend is still HELD (LF2's D+dir+A with
        // D held) — so a stray guard tap then walk+attack can't fire a special.
        else if (seqStage >= 1 && def && (newU || newDn || newL || newR)) {
            seqDir = newU ? 1 : newDn ? 2 : 0;
            if (newR) right = true; else if (newL) right = false;
            seqStage = 2; seqTimer = SEQ_WINDOW;
        }
        if (seqStage == 2 && fr && !wantSlot) {
            if (atk)      { wantSlot = seqDir==1?fr->hit_Ua:seqDir==2?fr->hit_Da:fr->hit_Fa; atkConsumed=true;  seqStage=0; }
            else if (jmp) { wantSlot = seqDir==1?fr->hit_Uj:seqDir==2?fr->hit_Dj:fr->hit_Fj; jumpConsumed=true; seqStage=0; }
        }

        // knockedDown used to be cleared ONLY inside the ST_LYING branch, so any
        // other way out of it (a light hit on a downed victim sends you to 220)
        // left the flag set forever: every later landing snapped to LYING and the
        // juggle test killed all knockback. Clear it whenever we are upright on
        // the ground and no longer in the fall/lie chain.
        if (knockedDown && grounded()) {
            int ks = f.state();
            if (ks != ST_FALLING && ks != ST_LYING) knockedDown = false;
        }

        // A frame chain can DROP INTO a falling frame on its own: the ice chain
        // ends at 182 and the burn timer sends you to 180. Flag it as a real
        // knockdown so the airborne branch HOLDS the tumble frames instead of
        // overwriting them with a jump pose, and so the landing goes to LYING.
        //
        // The launch velocity comes from the DATA where the data has one. Frame
        // 202 carries dvy -3, which drainFrameDvy() has already put into vy — so
        // by the time the chain reaches 182 the actor is airborne and we must not
        // overwrite it. Only a chain that arrives with no vertical motion of its
        // own (the burn timer's jump straight to 180) still needs a nudge, and
        // that -6 is a tuning constant with no source, so keep it minimal and
        // labelled rather than applying it to cases the data already covers.
        if (!knockedDown && f.state() == ST_FALLING) {
            knockedDown = true;
            if (grounded()) launch(-6.f);   // INFERENCE: no dvy in the data here
        }

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
            else if (atk && !atkConsumed) {                   // plain attack
                if (heldWeapon >= 0) {
                    // A HEAVY object can only be thrown — there is no swing for it,
                    // so plain Attack (no direction) hurls it the way you're facing.
                    // A light weapon still swings, and throws only with a direction.
                    if (heavyWeapon)   f.setFrame(fid::THROW_HEAVY);
                    else if (L || R)   f.setFrame(fid::THROW_LIGHT);
                    else               f.setFrame((swingId & 1) ? fid::WEAPON_ATTACK2
                                                                : fid::WEAPON_ATTACK);
                    ++swingId;
                } else throwPunch();
            }
            // Carrying a heavy object pins you to the ground in LF2 — no jumping.
            else if (jmp && !jumpConsumed && !(heldWeapon >= 0 && heavyWeapon)) {
                startJump(L || R);
            }
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
                    // Carrying a heavy weapon → overhead two-hand stance (12-15),
                    // never the normal recessed hold (0/5-8), and the .dat's
                    // slower heavy_walking speeds.
                    bool heavy = carryingHeavy();
                    float wsp  = heavy ? heavyWalkSpeed  : walkSpeed;
                    float wspz = heavy ? heavyWalkSpeedZ : walkSpeedZ;
                    if (moving) {
                        if (L) x -= wsp;
                        if (R) x += wsp;
                        if (U) z -= wspz;
                        if (D) z += wspz;
                        clampPos();
                        if (heavy) cycleAnim(fid::HEAVY_WALK, fid::HEAVY_WALK_LAST, walkRate);
                        else       cycleAnim(fid::WALKING,     fid::WALK_LAST,      walkRate);
                    } else if (heavy) {
                        if (f.frameId < fid::HEAVY_WALK || f.frameId > fid::HEAVY_WALK_LAST)
                            f.setFrame(fid::HEAVY_WALK);                    // hold overhead idle
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
            // A scripted charge (Louis frame 93: state 100, wait 90, dvx 23) is
            // capped by a very long `wait`; the real move ends when it runs out of
            // speed. Without this it kept the frame for the full 3 s, standing
            // still — so follow `next` (216, the dash recovery) as soon as friction
            // has eaten the charge. INFERENCE: state 100 is emulated by neither
            // OpenLF2 nor F.LF, so there is no reference for its exit condition.
            if (scriptedState(f.state()) && fabsf(f.vx) < MIN_SPEED) { f.gotoNext(); return; }
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
        int id = wantSlot; wantSlot = 0;
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

    // Holding a weapon, an attack while airborne/running/dashing THROWS it. The
    // bare-hand jump_attack (80-82) and run_attack (85-89) frames carry NO wpoint,
    // so playing them detaches the weapon (it floats). Faithful to LF2: weapon in
    // hand + attack = throw; the release impulse rides the throw frame's wpoint
    // (dennis 47/51/54). Returns true if it consumed the attack.
    bool throwHeldIfArmed() {
        if (heldWeapon < 0) return false;
        f.setFrame(heavyWeapon ? fid::THROW_HEAVY : fid::THROW_LIGHT);
        ++swingId;
        return true;
    }
    bool inPunch() const { return f.frameId >= fid::PUNCH && f.frameId <= fid::PUNCH2 + 3; }

    // A per-character HARDCODED state: the original engine has bespoke code for
    // these, and the .dat frames rely on it. Census of the 67 files:
    //   100      louis / louisEX  frame 93  dash_attack (wait 90, dvx 23 charge)
    //   301      deep             290-294   dash_sword (20 frames)
    //   400/401  woody            283/298   teleport
    //   500/501  rudolf           295/298   transform
    // We do not emulate what each one MEANS yet, but they must at least be
    // allowed to run their own frame chain instead of being overwritten by the
    // generic jump/dash pose — that is what broke Louis's run-jump-attack.
    static bool scriptedState(int s) { return s >= 100; }

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
            h = 0.f; vy = 0.f;
            // Landing ends an air attack (faithful to LF2/F.LF: touching the
            // ground snaps you out of the swing — do NOT let it finish airborne).
            // A SCRIPTED state is the exception: Louis's frame 93 is state 100,
            // wait 90, dvx 23 — a charge that is supposed to keep travelling and
            // exit through its own `next` (216). Cutting it on landing is what
            // made the move "start, freeze mid-way and end".
            if (scriptedState(f.state())) { f.advance(); return; }
            f.vx = 0.f;
            // …and landing must not snuff out a reaction state either: a victim
            // who catches fire or freezes in mid-air keeps burning/frozen on the
            // floor, and leaves through the burn timer / the ice chain's own
            // `next`, not through this line.
            int ls = f.state();
            if (ls == ST_BURNING || ls == ST_ICE) { f.advance(); return; }
            f.setFrame(knockedDown ? fid::LYING : fid::STANDING);
            return;
        }
        if (knockedDown) return;                // a knockdown holds its falling frame

        int s = f.state();
        // Already mid air-attack (jump_attack = state 3, dash_attack = state 15)
        // or in a hardcoded per-character state: let the frame graph play out.
        // BURNING (18) and ICE (13) belong here too: they are reaction states the
        // victim does not control, and overwriting them with a jump pose put the
        // fire out in mid-air — the burn timer then expired against a `jump`
        // state, its `f.state() == ST_BURNING` test failed, and the victim landed
        // on his feet instead of collapsing.
        if (s == ST_ATTACK || s == ST_SPECIAL || s == ST_BURNING || s == ST_ICE ||
            scriptedState(s)) { f.advance(); return; }
        // Attack pressed in the air → throw a held weapon, else jump/dash attack.
        if (atk) {
            if (throwHeldIfArmed()) return;
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
        if (atk) {                                                    // running attack (may cost MP)
            if (throwHeldIfArmed()) return;                           // armed → running throw
            const dat::Frame* rf = f.data ? f.data->frame(fid::RUN_ATTACK) : nullptr;
            int cost = rf ? rf->mp : 0;
            if (cost > 0 && f.mp < cost) return;
            if (cost > 0) f.mp -= cost;
            f.setFrame(fid::RUN_ATTACK); ++swingId; return;
        }
        // running jump = dash (but a heavy object keeps you grounded)
        if (jmp && !(heldWeapon >= 0 && heavyWeapon)) { startDash(); return; }
        bool keepDir = right ? (R && !L) : (L && !R);
        if (!keepDir) { f.setFrame(fid::STANDING); return; }
        float rsp  = carryingHeavy() ? heavyRunSpeed  : runSpeed;
        float rspz = carryingHeavy() ? heavyRunSpeedZ : runSpeedZ;
        x += right ? rsp : -rsp;
        if (U) z -= rspz;
        if (D) z += rspz;
        clampPos();
        if (carryingHeavy())
            cycleAnim(fid::HEAVY_RUN, fid::HEAVY_RUN_LAST, runRate);   // 16→18 loop
        else
            cycleAnim(fid::RUNNING, fid::RUN_LAST, runRate);           // 9→11 loop
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

    void enterRun() {
        f.setFrame((heldWeapon >= 0 && heavyWeapon) ? fid::HEAVY_RUN : fid::RUNNING);
        animTimer = 0; tapTimer = 0;
    }

    // ── Sub-behaviours ───────────────────────────────────────────────────────
    // Launch only: sets the arc; gravity is integrated by the airborne branch of
    // tick() from the next frame on.
    void startJump(bool moving) {
        f.setFrame(fid::JUMP);                 // state 4
        launch(jumpVy);                        // jump_height WINS over frame dvy
        f.vx = moving ? (right ? jumpDist : -jumpDist) : 0.f;
    }

    // Running jump: a low, fast forward leap (state 5, frames 213-214).
    void startDash() {
        f.setFrame(fid::DASH);                 // state 5
        launch(dashVy);                        // dash_height WINS over frame dvy
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
    // Hit-effect families (OpenLF2 const.c:135-146 + F.LF character.js:1721-1732).
    //   2, 21, 22, 23 → burn AND drop the held weapon
    //   20            → burn ("weak fire"), keeps the weapon
    //   3, 30         → freeze AND drop the weapon
    //   0 punch · 1 bleed · 4 shrafe → no state change, visual only
    static bool effectIsFire(int e)  { return e == 2 || (e >= 20 && e <= 23); }
    static bool effectIsIce(int e)   { return e == 3 || e == 30; }
    static bool effectDropsWeapon(int e) { return effectIsIce(e) || (effectIsFire(e) && e != 20); }

    // Frozen = anywhere in the ice chain, tested by FRAME ID and not by state.
    // The chain is 200 (state 15) → 201 (state 13, wait 90 = the 3 s freeze) →
    // 202 (state 15) → 182, so only the middle frame carries the "ice" state and
    // a state-only test leaves a two-tick hole at each end. Both consumers need
    // the whole range: one so a second ice hit cannot RESTART the chain, the
    // other so a hit lands as a shatter (F.LF character.js:1665 — a frozen victim
    // always falls down). Covering 200 and 201 but not 202 reintroduced exactly
    // the bug at the far end of the chain.
    bool iced() const {
        return f.state() == ST_ICE ||
               (f.frameId >= fid::ICE && f.frameId <= fid::ICE + 2);
    }

    int hit(int dmg, float kbx, int itrFall, int effect = 0) {
        // Vanished (next: 1280) = F.LF's effect.super: the bdy is skipped
        // entirely, so nothing can touch you until you reappear.
        if (untouchable()) return 0;
        // The itr `effect` veto (lf2.exe FUN_00417400 — see itrEffectAllows).
        // The caller applies the full rule, attacker state included; this is the
        // victim-only half, repeated here so NO path can burn a burning body.
        // It must run BEFORE the damage: the original rejects the pair outright,
        // it does not "hit for damage but skip the reaction" — which is what the
        // old guard down in the fire branch did, and why a victim standing in
        // Firen's ground fire kept bleeding HP while frozen in frame 203.
        if (!itrEffectAllows(effect, /*attackerState=*/-1, f.state(), f.frameId,
                             /*victimIsCharacter=*/true))
            return 0;
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

        // ── itr `effect`: fire and ice override the normal reaction ───────────
        // 450 itrs in the data carry a non-zero effect, and it was ignored
        // entirely — Firen's fire and Freeze's ice landed as plain damage.
        if (effectIsIce(effect)) {
            if (effectDropsWeapon(effect)) dropWeaponReq = true;
            if (!iced()) {
                fp = 0; burn = 0; knockedDown = false;
                f.vx = 0.f; h = 0.f; vy = 0.f;
                f.setFrame(fid::ICE);           // 200 → 201 (3 s) → 202 → falling
            }
            return lost;
        }
        if (effectIsFire(effect)) {
            if (effectDropsWeapon(effect)) dropWeaponReq = true;
            fp = 0;
            f.vx = 0.f;
            f.setFrame(fid::BURNING);           // 203, locked for BURN_TICKS
            burn = BURN_TICKS;
            return lost;
        }

        // Falling Points: each hit adds its `fall`. Over 60 (or death) → knocked
        // down; otherwise stay up and keep taking hits (a flurry accumulates FP
        // until it crosses 60). `kbx` carries the itr's own dvx (facing-signed):
        // flurry hits (dvx 2) barely nudge — keeping the victim in the combo —
        // while a launcher (dvx 12/fall 70) crosses 60 at once and throws.
        fp += itrFall;
        // F.LF fall(): besides the FP threshold, a hit ALWAYS fells you when you
        // are frozen (state 13) or already off the ground — that is what makes
        // ice shatter and what gives the game its anti-air.
        //   character.js:1665  if ($.state() == 13) { falldown() }
        //   character.js:1668  else if ($.ps.y < 0 || $.ps.vy < 0) { falldown() }
        bool frozen   = iced();
        bool airborne = !grounded();          // grounded() now folds in vy < 0
        if (fp > FP_FALL || f.hp <= 0 || frozen || airborne) {   // knockdown / juggle
            fp = 0;
            bool juggle = knockedDown;                // already airborne = re-hit
            knockedDown = true;
            f.setFrame(fid::FALLING);
            // The knockback is the engine's, not the frame's: discard the tumble
            // frame's own dvy so it cannot stack on top of the launch.
            if (juggle) { vy = -6.f; discardFrameDvy(); }   // re-loft, keep height
            else        { launch(-8.f); }
            // EVERY knockdown carries the itr's horizontal knockback, including a
            // re-hit on a victim already in the air.
            // SOURCE: lf2.exe 0x0042ee00-0x0042ef00 (lf2_decomp.c 81100-81190):
            // the knockback block does `injured->vx +/- itr->dvx` (the itr is
            // `local_74`, dvx is `local_74[5]` at +0x14), signed by the attacker's
            // facing, with no juggle special case anywhere in the chain.
            // We used to zero it for juggles ("keep the victim in place"), an
            // invention with no source: a second hit landing on a falling body
            // just bumped it, so Henry's charged arrow looked like it knocked the
            // target down on one shot and not on the next.
            float lv = std::fabs(kbx);
            if (!juggle && lv < 6.f) lv = 6.f;
            f.vx = (kbx < 0.f ? -lv : lv);
        } else {
            // The reaction frame is picked by the ACCUMULATED falling points, in
            // four tiers — not one generic flinch (F.LF character.js:1676-1686):
            //     fp <= 20 → 220     fp 21..30 → 222
            //     fp 31..40 → 224    fp 41..60 → 226  (Dance of Pain, state 16)
            // 226 is the long stun (wait 6 vs 2) and it is also the only state an
            // itr kind 1 can grab (OpenLF2: catch_injured requires injured_state_2
            // == 16), so getting the tier right is what will make catches work.
            int react = fid::INJURED;                 // 220
            if      (fp > FP_DOP) react = fid::DANCE_OF_PAIN;   // 226
            else if (fp > 30)     react = fid::INJURED + 4;     // 224
            else if (fp > 20)     react = fid::INJURED + 2;     // 222
            if (!f.data || !f.data->frame(react)) react = fid::INJURED;
            f.setFrame(react);
            x += kbx; clampPos();                     // dvx-sized nudge (a few px)
        }
        return lost;
    }
};

} // namespace lf2
