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
    p.comboNext = false; p.comboQueued = false; p.seqStage = 0;
    p.tick(false, true, false, false, true, false);       // holding forward + attack
    CHECK(p.f.frameId == lf2::fid::PUNCH, "walk-forward + attack throws a plain punch");

    // Rapid re-press BUFFERS the chain: punch2 fires when punch1 ENDS, and each
    // chained punch is a new swing (re-arms the per-swing hit gates).
    p.f.setFrame(lf2::fid::STANDING, false); p.comboNext = false; p.comboQueued = false;
    p.seqStage = 0;
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

    // Special = Square + direction. Mode A: direction held, tap Square → fires now.
    p.f.setFrame(lf2::fid::STANDING, false); p.right = true; p.h = 0.f;
    p.comboNext = false; p.comboQueued = false; p.seqStage = 0;
    p.tick(false, true, false,false, false,false, false, true);  // hold F + Square
    CHECK(p.f.frameId == 235, "hold-forward + Square fires hit_Fa special (235)");

    // Mode B: tap Square, then tap the direction within the window.
    // (spc is a LEVEL now — release it between taps / reset prevSpc.)
    p.f.setFrame(lf2::fid::STANDING, false); p.seqStage = 0;
    p.prevL = p.prevR = false; p.prevSpc = false;
    p.tick(false,false,false,false, false,false, false, true);   // tap Square (arms)
    p.tick(false, true, false,false, false,false, false, false); // tap forward → fires
    CHECK(p.f.frameId == 235, "Square then forward fires hit_Fa special (235)");

    // Expired window: Square, wait, then direction → just walks, no special.
    p.f.setFrame(lf2::fid::STANDING, false); p.seqStage = 0;
    p.prevR = false; p.prevSpc = false;
    p.tick(false,false,false,false, false,false, false, true);   // tap Square
    for (int i = 0; i < 20; ++i)                                  // window expires
        p.tick(false,false,false,false, false,false, false, false);
    p.tick(false, true, false,false, false,false, false, false); // forward after timeout
    CHECK(p.f.frameId != 235, "expired Square window does not fire the special");

    // Square + Jump = jump special (hit_Fj 280, c_foot) — and does NOT jump.
    p.f.setFrame(lf2::fid::STANDING, false); p.right = true; p.h = 0.f;
    p.seqStage = 0; p.prevSpc = false;
    p.tick(false,false,false,false, false, true, false, true);   // Square + Jump together
    CHECK(p.f.frameId == 280, "Square+Jump fires the jump special hit_Fj (280)");
    CHECK(p.grounded(),       "jump special does not also launch a jump");
    // ...and the armed order too: Square, then Jump.
    p.f.setFrame(lf2::fid::STANDING, false); p.seqStage = 0; p.h = 0.f; p.prevSpc = false;
    p.tick(false,false,false,false, false,false, false, true);   // tap Square (arms)
    p.tick(false,false,false,false, false, true, false, false);  // tap Jump
    CHECK(p.f.frameId == 280, "Square then Jump fires hit_Fj (280)");

    // The c_foot loop (284→287→284) is INTENTIONALLY infinite in the .dat; the
    // engine must break it by draining MP (frames carry mp:-17) and exit via
    // hit_d (288) when MP runs dry — otherwise the whirlwind slides forever.
    {
        int guard2 = 0;
        while (p.state() == lf2::ST_ATTACK && guard2++ < 3000)
            p.tick(false,false,false,false, false,false, false,false);
        CHECK(guard2 < 3000,           "c_foot loop terminates (MP drain breaks it)");
        CHECK(p.f.mp < p.f.maxMp,      "looping special drained MP");
    }
    // Early exit: pressing Defend during the loop bails out via hit_d (288).
    p.f.setFrame(lf2::fid::STANDING, false); p.seqStage = 0; p.h = 0.f;
    p.prevSpc = false; p.f.mp = p.f.maxMp;
    p.tick(false,false,false,false, false, true, false, true);   // Square+Jump → 280
    for (int i = 0; i < 12; ++i)                                  // into the loop
        p.tick(false,false,false,false, false,false, false,false);
    p.tick(false,false,false,false, false,false, true, false);   // press Defend
    CHECK(p.f.frameId >= 288 && p.f.frameId <= 290,
          "Defend mid-loop exits c_foot via hit_d (288)");

    // Neutral Square (tap alone, no direction, then release) = forward special.
    p.f.setFrame(lf2::fid::STANDING, false); p.right = true; p.h = 0.f;
    p.seqStage = 0; p.prevSpc = false;
    p.tick(false,false,false,false, false,false, false, true);   // press+hold Square (arms)
    p.tick(false,false,false,false, false,false, false, false);  // release → neutral special
    CHECK(p.f.frameId == 235, "neutral Square (tap) fires the forward special (235)");

    // Attack alone still punches (Square is the only special trigger).
    p.f.setFrame(lf2::fid::STANDING, false); p.seqStage = 0; p.comboNext = false;
    p.h = 0.f; p.vy = 0.f; p.prevSpc = false;
    p.tick(false, true, false, false, true, false, false, false); // walk + attack
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
    CHECK(!p.knockedDown && p.state() == lf2::ST_INJURED,
          "single fall-60 stays up (FP 60 is not > 60)");
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

    if (g_fail) { std::printf("\n%d CHECK(S) FAILED\n", g_fail); return 1; }
    std::printf("\nall player tests passed\n");
    return 0;
}
