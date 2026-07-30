// ─────────────────────────────────────────────────────────────────────────────
//  test_player — the frame-driven player controller against real dennis.dat
//
//  Skips gracefully if data/dennis.dat is absent (copyrighted, gitignored).
// ─────────────────────────────────────────────────────────────────────────────
#include "characters/player.hpp"
#include <cstdio>
#include <cmath>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } \
    else         { std::printf("  ok  : %s\n", msg); } \
} while (0)

static bool approx(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    dat::File dennis = dat::load("data/dennis.dat");
    if (dennis.frames.empty()) {
        std::printf("[player] data/dennis.dat not present — skipping\n");
        return 0;
    }
    std::printf("[player] dennis.dat loaded (%zu frames)\n", dennis.frames.size());

    lf2::Player p;
    p.load(&dennis);

    CHECK(p.state() == lf2::ST_STANDING, "starts standing (state 0)");
    CHECK(p.pic() == 0,                  "starts on pic 0");
    CHECK(approx(p.walkSpeed, 5.0f),     "walking_speed pulled from header (5)");
    CHECK(approx(p.jumpVy, -16.3f),      "jump_height pulled from header (-16.3)");

    // ── Walk right ───────────────────────────────────────────────────────────
    float x0 = p.x;
    p.tick(false, true, false, false, false, false);   // press R
    CHECK(p.right,                       "pressing R faces right");
    CHECK(p.state() == lf2::ST_WALKING,  "R enters walking (state 1)");
    CHECK(approx(p.x, x0 + p.walkSpeed),  "walking advances x by walking_speed");

    // ── Walk left flips facing ───────────────────────────────────────────────
    p.tick(true, false, false, false, false, false);   // press L
    CHECK(!p.right,                      "pressing L faces left");

    // ── Release → back to standing ───────────────────────────────────────────
    for (int i = 0; i < 3; ++i) p.tick(false,false,false,false,false,false);
    CHECK(p.state() == lf2::ST_STANDING, "no input returns to standing");

    // ── Punch (face right first; you can't turn mid-attack) ──────────────────
    p.tick(false, true, false, false, false, false);   // face right, walking
    for (int i = 0; i < 3; ++i) p.tick(false,false,false,false,false,false);
    p.tick(false,false,false,false, true, false);      // press attack
    CHECK(p.state() == lf2::ST_ATTACK,   "attack enters state 3 (punch)");
    CHECK(p.f.frameId == lf2::fid::PUNCH,"attack lands on canonical punch id 60");
    // Punch drifts forward a hair (frame 60 dv(1,0,0)).
    CHECK(p.f.vx > 0.f,                  "punch carries small forward dv facing right");
    // Let the punch animation run out; it must return to standing via next:999.
    int guard = 0;
    while (p.state() == lf2::ST_ATTACK && guard++ < 100)
        p.tick(false,false,false,false,false,false);
    CHECK(p.state() == lf2::ST_STANDING, "punch finishes back to standing");

    // ── Jump: leaves the ground, gravity brings it back ──────────────────────
    p.tick(false,false,false,false,false, true);       // press jump
    CHECK(p.state() == lf2::ST_JUMP,     "jump enters state 4");
    CHECK(!p.grounded(),                 "jump leaves the ground (h<0)");
    CHECK(approx(p.vy, p.jumpVy),        "jump launches at jump_height (gravity next tick)");
    p.tick(false,false,false,false,false,false);        // one airborne tick
    CHECK(approx(p.vy, p.jumpVy + GRAVITY), "gravity accrues once airborne");
    guard = 0;
    while (!p.grounded() && guard++ < 300)
        p.tick(false,false,false,false,false,false);
    CHECK(p.grounded(),                  "gravity returns the jump to the ground");
    CHECK(p.state() == lf2::ST_STANDING, "landing returns to standing");

    // ── Double-tap to run ────────────────────────────────────────────────────
    p.f.setFrame(lf2::fid::STANDING, false);
    p.right = true; p.prevL = p.prevR = false; p.tapDir = 0; p.tapTimer = 0;
    p.tick(false, true, false, false, false, false);   // tap 1
    p.tick(false, false, false, false, false, false);  // release
    float rx = p.x;
    p.tick(false, true, false, false, false, false);   // tap 2 → run
    CHECK(p.state() == lf2::ST_RUNNING, "double-tap R enters running (state 2)");
    p.tick(false, true, false, false, false, false);   // run stride
    CHECK(p.x > rx + p.walkSpeed, "running covers more ground than walking");
    // Walking animation actually cycles through frames 5..8 (not stuck on 5).
    p.f.setFrame(lf2::fid::STANDING, false); p.prevR = false; p.tapDir = 0;
    bool advanced = false;
    for (int i = 0; i < 12; ++i) {
        p.tick(false, true, false, false, false, false);
        if (p.f.frameId > lf2::fid::WALKING && p.f.frameId <= lf2::fid::WALK_LAST) advanced = true;
    }
    CHECK(advanced, "walking cycles past frame 5 (animation not frozen)");

    // Running jump = dash (state 5), a faster forward leap than a normal jump.
    p.f.setFrame(lf2::fid::RUNNING, false);
    p.right = true; p.h = 0.f; p.vy = 0.f;
    p.tick(false, true, false, false, false, true);    // run + jump
    CHECK(p.state() == lf2::ST_DASH,     "running jump enters dash (state 5)");
    CHECK(!p.grounded() && p.f.vx > p.jumpDist, "dash leaps faster forward than a jump");

    // Defend: holding the guard button raises the shield; releasing drops it.
    p.f.setFrame(lf2::fid::STANDING, false); p.h = 0.f; p.vy = 0.f;
    p.tick(false,false,false,false, false,false, true);   // hold defend
    CHECK(p.state() == lf2::ST_DEFEND,   "holding defend raises guard (state 7)");
    p.tick(false,false,false,false, false,false, false);  // release
    CHECK(p.state() == lf2::ST_STANDING, "releasing defend returns to standing");

    // Anchor semantics: player.x IS the anchor (objectX). Across punch frames
    // (centerx 42→27→25→35) the anchor must stay put while the draw origin
    // shifts under it — the cell slides, the body doesn't.
    p.f.setFrame(lf2::fid::STANDING, false); p.right = true; p.h = 0.f;
    p.x = 500.f; p.syncAnchor();
    p.tick(false,false,false,false, true, false);          // punch1 (frame 60, cx 42)
    CHECK(approx(p.f.x, p.x), "fighter anchor tracks player.x exactly");
    float dx0, dy0; p.f.drawOrigin(dx0, dy0);
    float xBefore = p.x;
    guard = 0;                                             // wait:2 → 3 ticks to frame 61
    while (p.f.frameId == lf2::fid::PUNCH && guard++ < 10)
        p.tick(false,false,false,false, false, false);
    CHECK(p.f.frameId == lf2::fid::PUNCH + 1, "punch advanced to frame 61 (cx 27)");
    float dx1, dy1; p.f.drawOrigin(dx1, dy1);
    float anchorMove = p.x - xBefore;                      // frame dvx drift only
    // Draw origin must shift by anchorMove + (cx60 - cx61) = anchorMove + 15:
    // the CELL slides under the fixed body. Under the old cell-pinned bug this
    // difference was 0 (cell fixed, body jittered).
    CHECK(std::fabs((dx1 - dx0) - anchorMove - 15.f) < 0.6f,
          "cell slides by centerx delta while the body anchor stays put");

    // Walking + attack = PLAIN PUNCH (holding a direction must NOT fire specials).
    p.f.setFrame(lf2::fid::STANDING, false); p.right = true; p.h = 0.f;
    p.comboNext = false; p.comboQueued = false;
    p.tick(false, true, false, false, true, false);       // holding forward + attack
    CHECK(p.f.frameId == lf2::fid::PUNCH, "walk-forward + attack throws a plain punch");

    // Rapid re-press BUFFERS the chain: punch2 fires when punch1 ENDS, and each
    // chained punch is a new swing (re-arms the per-swing hit gates).
    p.f.setFrame(lf2::fid::STANDING, false); p.comboNext = false; p.comboQueued = false;

    int swing0 = p.swingId;
    p.tick(false,false,false,false, true, false);         // attack → punch1
    CHECK(p.f.frameId == lf2::fid::PUNCH,  "attack throws punch1 (60)");
    p.tick(false,false,false,false, true, false);         // re-press during punch1
    CHECK(p.f.frameId >= lf2::fid::PUNCH && p.f.frameId <= lf2::fid::PUNCH + 3,
          "re-press mid-punch buffers (does not restart the animation)");
    guard = 0;
    while (p.f.frameId != lf2::fid::PUNCH2 && guard++ < 60)
        p.tick(false,false,false,false, false, false);
    CHECK(p.f.frameId == lf2::fid::PUNCH2, "buffered chain fires punch2 when punch1 ends");
    CHECK(p.swingId == swing0 + 2,          "chained punch counts as a new swing");

    // Square + Directional (bare, no A) = the D+dir+A move (hit_?a). Forward=235.
    auto sq = [&]{ p.f.setFrame(lf2::fid::STANDING, false); p.right = true;
                   p.h = 0.f; p.vy = 0.f; p.comboNext = false; p.comboQueued = false;
                   p.seqStage = 0; p.f.mp = p.f.maxMp;   // MP starts at 200 in-game
                   p.prevSpc = p.prevL = p.prevR = p.prevU = p.prevDn = p.prevDef = false; };
    sq();
    p.tick(false, true, false,false, false,false, false, true);  // hold Forward + Square
    CHECK(p.f.frameId == 235, "Square + forward (bare) fires hit_Fa (235)");

    // Square is FORWARD-ONLY: Square + Down does NOT fire the vertical special.
    sq();
    p.tick(false,false, false,true,  false,false, false, true);  // Square + Down
    CHECK(p.f.frameId != 265, "Square + down does NOT fire hit_Da (Square = forward only)");

    // Faithful command Defend → Down → Attack fires the vertical special (Da 265).
    sq();
    p.tick(false,false,false,false, false,false, true, false);   // tap Defend (guard)
    p.tick(false,false,false,true,  false,false, true, false);   // Down (still guarding)
    p.tick(false,false,false,false, true, false, true, false);   // Attack → hit_Da
    CHECK(p.f.frameId == 265, "Defend->Down->Attack fires hit_Da (Shrafe 265)");

    // Neutral Square (no direction) = forward special (hit_Fa).
    sq();
    p.tick(false,false,false,false, false,false, false, true);
    CHECK(p.f.frameId == 235, "neutral Square fires the forward special (235)");

    // Square + Directional + Jump = the D+dir+J jump-special (hit_?j). Fwd=280.
    sq();
    p.tick(false, true, false,false, false, true, false, true);  // Forward + Jump + Square
    CHECK(p.f.frameId == 280, "Square + forward + Jump fires hit_Fj (280)");
    CHECK(p.grounded(),       "jump special does not also launch a jump");

    // c_foot loop (284→287→284) is INTENTIONALLY infinite; the engine breaks it
    // by draining MP (frames carry mp:-17), exiting via hit_d (288) when dry.
    {
        int guard2 = 0;
        while (p.state() == lf2::ST_ATTACK && guard2++ < 3000)
            p.tick(false,false,false,false, false,false, false,false);
        CHECK(guard2 < 3000,      "c_foot loop terminates (MP drain breaks it)");
        CHECK(p.f.mp < p.f.maxMp, "looping special drained MP");
    }

    // Attack alone still punches (Square is the only special trigger).
    sq();
    p.tick(false, true, false, false, true, false, false, false); // walk + attack, no Square
    CHECK(p.f.frameId == lf2::fid::PUNCH, "attack never fires specials without Square");

    // Jump attack: pressing attack in the air enters the jump-attack frames.
    p.f.setFrame(lf2::fid::STANDING, false); p.h = 0.f; p.vy = 0.f;
    p.tick(false,false,false,false, false, true);         // jump
    CHECK(!p.grounded(),                 "jump left the ground");
    p.tick(false,false,false,false, true, false);         // attack airborne
    CHECK(p.f.frameId == lf2::fid::JUMP_ATTACK, "air attack enters jump_attack (frame 80)");

    // ── Combat reactions ─────────────────────────────────────────────────────
    auto reset = [&]() {
        p.f.hp = p.f.maxHp; p.h = 0.f; p.vy = 0.f; p.knockedDown = false;
        p.fp = 0;
        p.right = true; p.f.setFrame(lf2::fid::STANDING, false);
    };

    // Single weak hit (fall 25) → stagger in place, NOT a knockdown.
    reset();
    int lost = p.hit(20, -10.f, /*fall=*/25);
    CHECK(lost == 20,                     "weak hit removes 20 HP");
    CHECK(p.state() == lf2::ST_INJURED,   "weak hit → stagger (state 11), no knockdown");
    CHECK(!p.knockedDown,                 "weak hit does not knock down");
    guard = 0;
    while (p.state() == lf2::ST_INJURED && guard++ < 100)
        p.tick(false,false,false,false,false,false);
    CHECK(p.state() == lf2::ST_STANDING,  "stagger recovers to standing");

    // A rapid sequence of weak hits accumulates FP (25,50,75) past 60 → knockdown.
    reset();
    p.hit(20, -10.f, 25); CHECK(!p.knockedDown, "combo hit 1 (FP 25): still standing");
    p.hit(20, -10.f, 25); CHECK(!p.knockedDown, "combo hit 2 (FP 50): still standing");
    p.hit(20, -10.f, 25); CHECK(p.knockedDown,  "combo hit 3 (FP 75 > 60): knockdown");

    // A single fall-60 hit is FP 60 — NOT over 60 — so it staggers, doesn't fall.
    reset();
    p.hit(40, -10.f, /*fall=*/60);
    // FP 60 is over the DoP threshold (40), so the reaction is the LONG stun,
    // frame 226 / state 16 — not the brief 220 flinch (F.LF character.js:1676).
    CHECK(!p.knockedDown && p.state() == lf2::ST_INJURED2,
          "single fall-60 stays up, in Dance of Pain (state 16)");
    CHECK(p.f.frameId == lf2::fid::DANCE_OF_PAIN, "fp 41-60 picks frame 226");
    // A single fall-70 launcher (FP 70 > 60) knocks down at once.
    reset();
    p.hit(40, -10.f, /*fall=*/70);
    CHECK(p.knockedDown,                  "single fall-70 launcher knocks down at once");
    CHECK(p.state() == lf2::ST_FALLING,   "launcher → falling (state 12)");
    guard = 0;
    while (p.state() != lf2::ST_LYING && guard++ < 300)
        p.tick(false,false,false,false,false,false);
    CHECK(p.state() == lf2::ST_LYING,     "knockdown lands into lying (state 14)");
    CHECK(p.alive(),                      "survivable knockdown stays alive");
    guard = 0;
    while (p.state() == lf2::ST_LYING && guard++ < 200)
        p.tick(false,false,false,false,false,false);
    CHECK(p.state() != lf2::ST_LYING && !p.knockedDown, "alive actor gets up from lying");

    // Front defend blocks a light hit entirely.
    reset();
    p.defend();
    CHECK(p.isDefending(),                "defend enters guard (state 7)");
    int hpBefore = p.hp();
    int blocked = p.hit(20, -30.f, /*fall=*/25);       // struck from the front
    CHECK(blocked == 0 && p.hp() == hpBefore, "front defend fully blocks a light hit");
    CHECK(p.f.frameId == lf2::fid::DEFEND + 1, "blocking shows the guard-recoil frame (111)");

    // Fatal knockdown → dead, pinned lying.
    reset();
    p.f.hp = 10;
    p.hit(999, -10.f, /*fall=*/60);
    guard = 0;
    while (p.state() != lf2::ST_LYING && guard++ < 300)
        p.tick(false,false,false,false,false,false);
    CHECK(p.dead() && !p.alive(),         "fatal knockdown is dead once lying");
    int fid_before = p.f.frameId;
    for (int i = 0; i < 100; ++i) p.tick(false,false,false,false,false,false);
    CHECK(p.state() == lf2::ST_LYING && p.f.frameId == fid_before,
          "dead actor stays pinned lying (never gets up)");

    // ── Sheet mapping: real boundaries 0-69 / 70-139 / 140-209 ───────────────
    int ord, loc;
    p.f.setFrame(0);
    CHECK(p.f.sheetLocal(ord, loc) && ord == 0 && loc == 0, "pic 0 -> sheet 0 local 0");

    // ── Ground friction kills leftover vx (F.LF ps.fric = 1) ─────────────────
    // `dvx: 0` means KEEP, and full attack chains are dvx 0 (Rudolf's shuriken
    // frames), so without friction a stale velocity slid the fighter for the
    // whole animation.
    {
        lf2::Player q; q.load(&dennis);
        q.f.setFrame(lf2::fid::STANDING, false);
        q.f.vx = -8.f;                       // leftover knockback/run velocity
        float x0 = q.x;
        for (int i = 0; i < 12; ++i) q.tick(0,0,0,0, false,false);
        CHECK(std::fabs(q.f.vx) < 0.01f, "ground friction zeroes leftover vx");
        CHECK(q.x > x0 - 60.f, "fighter stops sliding instead of drifting away");
    }

    // ── Falling Points: who goes down, and after how many hits ───────────────
    // Values are the real ones in the .dat, so this pins the behaviour that a
    // combo only launches when it COMPLETES, and that a normal arrow has to land
    // twice. FP adds each itr's `fall`, knocks down above 60, and decays 0.45/tick.
    {
        // henry_arrow1: fall 60 — and 60 is NOT > 60, so one arrow never fells.
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.hit(40, -9.f, 60);
        CHECK(v.f.state() != lf2::ST_FALLING, "one arrow (fall 60) staggers but does not fell");
        v.hit(40, -9.f, 60);
        CHECK(v.f.state() == lf2::ST_FALLING, "the second arrow (fp 120) fells");
    }
    {
        // henry_arrow2, the charged shot: fall 70 — a launcher, fells at once.
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.hit(50, -20.f, 70);
        CHECK(v.f.state() == lf2::ST_FALLING, "charged arrow (fall 70) fells in one hit");
    }

    // ── Special `next` codes above 999 ───────────────────────────────────────
    // Only three exist in the whole data set: 1000 (remove, covered by
    // test_object), 1280 and a negative next.
    {
        // next: 1280 — Rudolf's "disappear" (hit_Uj 250 → … → 257). The fighter
        // goes back to standing AND turns invisible + untouchable for ~150 ticks
        // (F.LF hides sprite/shadow and sets effect.super).
        dat::File rudolf = dat::load("data/rudolf.dat");
        if (!rudolf.frames.empty()) {
            lf2::Player p; p.load(&rudolf);
            p.x = 400.f; p.z = 400.f; p.syncAnchor();
            p.f.mp = p.f.maxMp;
            p.f.setFrame(250);
            int appeared = -1, vanishedAt = -1, damage = 0;
            for (int t = 0; t < 260; ++t) {
                p.tick(0,0,0,0,false,false);
                if (p.hidden() && vanishedAt < 0) vanishedAt = t;
                if (vanishedAt >= 0 && !p.hidden() && appeared < 0) appeared = t;
                if (p.hidden()) damage += p.hit(100, -5.f, 70);
            }
            CHECK(vanishedAt >= 0, "next 1280 makes the fighter vanish");
            CHECK(damage == 0, "a vanished fighter cannot be damaged (effect.super)");
            CHECK(appeared > vanishedAt, "the fighter comes back on its own");
            CHECK(appeared - vanishedAt > 120 && appeared - vanishedAt < 180,
                  "vanish lasts about 150 ticks (5 s)");
            CHECK(p.f.hp == p.f.maxHp, "no damage got through while hidden");
        }
    }
    {
        // A NEGATIVE next means "go to |next| and flip the facing" (F.LF
        // switch_dir_after_trans). Louis's c-throw, frame 270, next -999: he
        // turns around as he hurls the victim behind him.
        dat::File louis = dat::load("data/louis.dat");
        if (!louis.frames.empty()) {
            const dat::Frame* fr = louis.frame(270);
            CHECK(fr && fr->next == -999, "louis frame 270 really carries next -999");
            lf2::Player p; p.load(&louis);
            p.x = 400.f; p.z = 400.f; p.right = true; p.syncAnchor();
            p.f.setFrame(270);
            bool before = p.right;
            for (int t = 0; t < 10; ++t) p.tick(0,0,0,0,false,false);
            CHECK(p.right != before, "negative next flips the facing");
            CHECK(p.f.frameId == lf2::fid::STANDING, "…and still lands on |999| = standing");
        }
    }

    // ── itr `effect`: fire and ice (OpenLF2 const.c:135-146) ─────────────────
    // 450 itrs in the data carry an effect and it used to be ignored entirely.
    {
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.hit(30, -2.f, 20, /*effect=*/3);          // freeze
        CHECK(v.f.frameId == lf2::fid::ICE, "effect 3 freezes the victim (frame 200)");
        // 200 (wait 2) → 201 is state 13 and waits 90 ticks = 3 s frozen.
        for (int i = 0; i < 5; ++i) v.tick(0,0,0,0,false,false);
        CHECK(v.state() == lf2::ST_ICE, "the ice chain parks in state 13");
        int t = 0;
        while (v.state() == lf2::ST_ICE && t++ < 200) v.tick(0,0,0,0,false,false);
        CHECK(t > 60 && t < 130, "the freeze lasts about 90 ticks (3 s)");
    }
    {
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.hit(30, -2.f, 20, /*effect=*/2);          // fire
        CHECK(v.f.frameId == lf2::fid::BURNING, "effect 2 sets the victim on fire (203)");
        CHECK(v.state() == lf2::ST_BURNING,     "burning is state 18");
        CHECK(v.dropWeaponReq, "fire makes the victim drop a held weapon");
        // The 203/204 pair loops forever in the data: the burn timer is what ends it.
        for (int i = 0; i < lf2::Player::BURN_TICKS + 2; ++i) v.tick(0,0,0,0,false,false);
        CHECK(v.state() != lf2::ST_BURNING, "the burn ends instead of looping forever");
        // Weak fire must not restart the animation on someone already burning.
        lf2::Player w; w.load(&dennis); w.x = 400.f; w.z = 400.f; w.syncAnchor();
        w.hit(10, -2.f, 20, 2);
        w.dropWeaponReq = false;
        w.hit(10, -2.f, 20, 20);
        CHECK(!w.dropWeaponReq, "effect 20 (weak fire) does not disarm a burning victim");
    }

    // ── Hardcoded per-character states must run their own frame chain ────────
    // Louis frame 93 is state 100 (wait 90, dvx 23): a charge. airborneTick only
    // let states 3/15 animate, so the generic jump pose overwrote it — the move
    // started, froze mid-way and ended. Same trap would hit deep (301),
    // woody (400/401) and rudolf (500/501).
    {
        dat::File louis = dat::load("data/louis.dat");
        if (!louis.frames.empty()) {
            const dat::Frame* f93 = louis.frame(93);
            CHECK(f93 && f93->state == 100, "louis frame 93 really is state 100");
            lf2::Player p2; p2.load(&louis);
            p2.x = 400.f; p2.z = 400.f; p2.right = true; p2.syncAnchor();
            p2.f.setFrame(lf2::fid::RUNNING, false);
            p2.tick(0,1,0,0, false, true);                  // run + jump = dash
            p2.tick(0,0,0,0, true,  false);                  // attack in the air
            CHECK(p2.f.frameId == lf2::fid::DASH_ATTACK, "run+jump+attack enters 90");
            float sx = p2.x;
            int inCharge = 0;
            for (int t = 0; t < 140; ++t) {
                p2.tick(0,0,0,0,false,false);
                if (p2.f.state() == 100) ++inCharge;
            }
            CHECK(inCharge > 10, "the state-100 charge actually runs (not overwritten)");
            CHECK(p2.x - sx > 200.f, "the charge carries Louis forward a long way");
            CHECK(p2.f.state() != 100, "and it exits instead of holding the frame for 3 s");
        }
    }

    // ── Achados da revisão externa (2026-07-29) ──────────────────────────────
    {   // #1 knockedDown ficava preso: um golpe leve em quem estava deitado saía
        // de ST_LYING sem limpar a flag, e todo pulo seguinte terminava em 230.
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.hit(10, -5.f, 70);                       // derruba
        int g2 = 0;
        while (v.state() != lf2::ST_LYING && g2++ < 120) v.tick(0,0,0,0,false,false);
        CHECK(v.knockedDown, "um fall:70 derruba e marca knockedDown");
        v.hit(10, -2.f, 10);                       // golpe leve em quem está no chão
        for (int i = 0; i < 60; ++i) v.tick(0,0,0,0,false,false);
        CHECK(!v.knockedDown, "knockedDown é limpo ao voltar a ficar de pé");
    }
    {   // #6 alvo no ar deve ser derrubado por qualquer golpe (F.LF fall()).
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.tick(0,0,0,0,false,true);                // pula
        CHECK(!v.grounded(), "está no ar");
        v.hit(10, -2.f, 10);                       // jab fraco
        CHECK(v.knockedDown, "golpe fraco em alvo no ar derruba (anti-aéreo)");
    }
    {   // #5 alvo congelado estilhaça com qualquer golpe.
        lf2::Player v; v.load(&dennis); v.x = 400.f; v.z = 400.f; v.syncAnchor();
        v.hit(10, -2.f, 20, /*effect=*/3);
        for (int i = 0; i < 6; ++i) v.tick(0,0,0,0,false,false);
        CHECK(v.state() == lf2::ST_ICE, "congelado");
        v.hit(10, -2.f, 10);                       // golpe fraco no gelo
        CHECK(v.knockedDown, "golpe em alvo congelado sempre derruba");
    }

    if (g_fail) { std::printf("\n%d CHECK(S) FAILED\n", g_fail); return 1; }
    std::printf("\nall player tests passed\n");
    return 0;
}
