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

    // ── Combat reactions ─────────────────────────────────────────────────────
    auto reset = [&]() {
        p.f.hp = p.f.maxHp; p.h = 0.f; p.vy = 0.f; p.knockedDown = false;
        p.right = true; p.f.setFrame(lf2::fid::STANDING, false);
    };

    // Light hit → injured stagger, HP drops, recovers to standing.
    reset();
    int lost = p.hit(20, -10.f, /*heavy=*/false);
    CHECK(lost == 20,                     "light hit removes 20 HP");
    CHECK(p.state() == lf2::ST_INJURED,   "light hit → injured (state 11)");
    guard = 0;
    while (p.state() == lf2::ST_INJURED && guard++ < 100)
        p.tick(false,false,false,false,false,false);
    CHECK(p.state() == lf2::ST_STANDING,  "injured recovers to standing");

    // Heavy hit (survivable) → knockdown → lying → gets back up.
    reset();
    p.hit(40, -10.f, /*heavy=*/true);
    CHECK(p.knockedDown,                  "heavy hit sets knockdown");
    CHECK(p.state() == lf2::ST_FALLING,   "heavy hit → falling (state 12)");
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
    int blocked = p.hit(20, -30.f, /*heavy=*/false);   // struck from the front
    CHECK(blocked == 0 && p.hp() == hpBefore, "front defend fully blocks a light hit");

    // Fatal knockdown → dead, pinned lying.
    reset();
    p.f.hp = 10;
    p.hit(999, -10.f, /*heavy=*/true);
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
