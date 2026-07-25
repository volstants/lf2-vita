// ─────────────────────────────────────────────────────────────────────────────
//  test_object — projectile runtime against the real dennis_ball.dat, plus the
//  opoint plumbing on dennis.dat and the data.txt oid resolution.
//  Skips gracefully when the (gitignored) game data is absent.
// ─────────────────────────────────────────────────────────────────────────────
#include "engine/object.hpp"
#include "characters/player.hpp"
#include <cstdio>
#include <cmath>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } \
    else         { std::printf("  ok  : %s\n", msg); } \
} while (0)

int main() {
    dat::File ball = dat::load("data/dennis_ball.dat");
    if (ball.frames.empty()) { std::printf("[object] data absent — skipping\n"); return 0; }
    std::printf("[object] dennis_ball.dat: %zu frames\n", ball.frames.size());

    // ── Flight ───────────────────────────────────────────────────────────────
    lf2::Object o;
    o.spawn(&ball, 0, 100.f, 300.f, 400.f, /*right=*/true, 0, /*team=*/0);
    CHECK(o.active && o.flying(),      "spawns active in flying state (3000)");
    CHECK(std::fabs(o.f.vx - 15.f) < 0.01f, "flying frame sets vx = 15");
    float x0 = o.f.x;
    for (int i = 0; i < 10; ++i) o.tick();
    CHECK(o.f.x > x0 + 140.f,          "ball travels ~15 px/tick");
    CHECK(o.flying(),                  "flying loop (next:999 -> frame 0) keeps flying");

    // Facing left mirrors the velocity.
    lf2::Object ol;
    ol.spawn(&ball, 0, 500.f, 300.f, 400.f, /*right=*/false, 0, 0);
    ol.tick();
    CHECK(ol.f.x < 500.f,              "facing-left ball flies left");

    // ── Hit → hiting frames → removed at next:1000 ───────────────────────────
    o.onHit();
    CHECK(o.f.frameId == lf2::oid_frame::HITING, "onHit enters the hiting frames (10)");
    CHECK(std::fabs(o.f.vx) < 0.01f,   "onHit stops the ball");
    int guard = 0;
    while (o.active && guard++ < 60) o.tick();
    CHECK(!o.active,                   "hiting chain removes the ball (next:1000)");

    // Off-map despawn.
    lf2::Object om;
    om.spawn(&ball, 0, (float)MAP_W + 150.f, 300.f, 400.f, true, 0, 0);
    for (int i = 0; i < 10 && om.active; ++i) om.tick();
    CHECK(!om.active,                  "ball despawns off-map");

    // ── data.txt: oid 205 resolves to dennis_ball ────────────────────────────
    dat::Index ix = dat::loadIndex("data/data.txt");
    const dat::ObjectEntry* e205 = ix.object(205);
    CHECK(e205 && e205->file.find("dennis_ball") != std::string::npos,
          "data.txt: oid 205 -> dennis_ball.dat");

    // ── opoint on the fireball frames of dennis.dat ──────────────────────────
    dat::File dennis = dat::load("data/dennis.dat");
    if (!dennis.frames.empty()) {
        bool foundOp = false; int opFrame = -1, opOid = 0;
        for (const auto& fr : dennis.frames)
            for (const auto& op : fr.opoints)
                if (op.kind == 1 && !foundOp && fr.id >= 235 && fr.id <= 241) {
                    foundOp = true; opFrame = fr.id; opOid = op.oid;
                }
        CHECK(foundOp && opOid == 205,
              "dennis fireball frames carry an opoint with oid 205");
        std::printf("        (opoint no frame %d)\n", opFrame);

        // MP: firing the special deducts the entry frame's mp cost.
        lf2::Player p; p.load(&dennis);
        const dat::Frame* f235 = dennis.frame(235);
        int cost = f235 ? f235->mp : 0;
        std::printf("        (custo mp do frame 235: %d)\n", cost);
        int mp0 = p.f.mp;
        p.prevSpc = false; p.right = true;
        p.tick(false, true, false, false, false, false, false, true); // F+Square
        CHECK(p.f.frameId == 235,      "special still fires with full MP");
        if (cost > 0)
            CHECK(p.f.mp <= mp0 - cost + 1, "special deducts the frame's mp cost");
        // Drain MP → special must whiff.
        lf2::Player p2; p2.load(&dennis);
        p2.f.mp = 0; p2.prevSpc = false; p2.right = true;
        p2.tick(false, true, false, false, false, false, false, true);
        if (cost > 0)
            CHECK(p2.f.frameId != 235, "empty MP blocks the special");
    }

    // ── Depth collision: the z-fix (ball inherits spawner z) ─────────────────
    // A flying ball registers a hit against a body ON THE SAME z-line, and
    // misses one on a different line. This is the exact bug the "ball at the
    // hands but no collision" symptom pointed at: z never affects the draw
    // position, only this depth band. Uses the SAME box + ±12 z test the
    // (working) melee collision uses in main.cpp.
    if (!dennis.frames.empty()) {
        auto ballAtkBox = [](lf2::Object& ob, Box& out) {
            bool got = false;
            ob.f.forEachItr([&](const lf2::WBox& wb, const dat::Itr& it) {
                if (it.kind == 0 && !got) {
                    out = { (int)wb.x, (int)wb.y, (int)wb.w, (int)wb.h }; got = true;
                }
            });
            return got;
        };
        auto bodyBox = [](lf2::Player& pl, Box& out) {
            bool got = false;
            pl.f.forEachBdy([&](const lf2::WBox& wb, const dat::Bdy&) {
                if (!got) { out = { (int)wb.x, (int)wb.y, (int)wb.w, (int)wb.h }; got = true; }
            });
            return got;
        };
        lf2::Player victim; victim.load(&dennis);
        victim.x = 300.f; victim.z = 400.f; victim.right = false; victim.syncAnchor();

        lf2::Object b;                                  // spawn ON the victim's line
        b.spawn(&ball, 0, 300.f, victim.f.y, 400.f, true, 0, 0);
        Box atk, body;
        bool sameLine = ballAtkBox(b, atk) && bodyBox(victim, body) &&
                        std::fabs(b.f.z - victim.z) <= 12.f && boxOverlap(atk, body);
        CHECK(sameLine, "ball hits a body on the SAME z-line (depth fix works)");

        lf2::Object b2;                                 // spawn 60 px deeper
        b2.spawn(&ball, 0, 300.f, victim.f.y, 460.f, true, 0, 0);
        bool offLine = std::fabs(b2.f.z - victim.z) <= 12.f;
        CHECK(!offLine, "ball on a different z-line is correctly out of range");
    }

    // ── Weapon object: rests on the ground, frozen while held ────────────────
    dat::File knife = dat::load("data/weapon4.dat");
    if (!knife.frames.empty()) {
        lf2::Object w;
        w.spawn(&knife, 0, 700.f, 365.f, 365.f, true, lf2::weapon_frame::ON_GROUND, 0);
        w.weaponType = 1;
        w.restOnGround(700.f, 365.f, 365.f, true);
        CHECK(w.f.frameId == lf2::weapon_frame::ON_GROUND, "weapon rests on frame 64");
        float wx = w.f.x;
        for (int i = 0; i < 30; ++i) w.tick();
        CHECK(std::fabs(w.f.x - wx) < 0.01f && w.active, "grounded weapon stays put (no drift/despawn)");
        w.held = true; w.f.x = 123.f;
        for (int i = 0; i < 10; ++i) w.tick();
        CHECK(std::fabs(w.f.x - 123.f) < 0.01f, "held weapon is frozen (holder positions it)");

        // <weapon_strength_list>: the real damage of a swing, per wpoint.attacking.
        CHECK(knife.strength[1].valid && knife.strength[1].injury == 45 &&
              knife.strength[1].fall == 40, "strength[1] normal: injury 45, fall 40");
        CHECK(knife.strength[4].valid && knife.strength[4].injury == 55 &&
              knife.strength[4].fall == 70, "strength[4] dash: injury 55, fall 70");
        // A held weapon's own frame carries a kind-2 wpoint (the grip point) and
        // a kind-5 itr (its strike box) — both required by the hold/attack code.
        const dat::Frame* onHand = knife.frame(23);
        bool grip = false, k5 = false;
        if (onHand) {
            for (const auto& wpt : onHand->wpoints) if (wpt.kind == 2) grip = true;
            for (const auto& it : onHand->itrs)     if (it.kind == 5)  k5 = true;
        }
        CHECK(grip, "on_hand frame has its own kind-2 wpoint (grip alignment)");
        CHECK(k5,   "on_hand frame has a kind-5 itr (weapon strike box)");

        // Throw: flies forward, gravity pulls it down, lands and rests.
        lf2::Object t;
        t.spawn(&knife, 0, 500.f, 300.f, 400.f, true, lf2::weapon_frame::ON_GROUND, 0);
        t.weaponType = 1; t.groundY = 400.f;
        t.throwFrom(500.f, 400.f, 400.f, /*right=*/true, 12.f, -8.f, 0.f);
        CHECK(t.thrown && !t.held,    "throwFrom marks the weapon as airborne");
        CHECK(t.f.x > 500.f,          "thrown weapon is placed ahead of the holder");
        float tx = t.f.x;
        t.tick();
        CHECK(t.f.x > tx,             "thrown weapon travels forward");
        int g2 = 0;
        while (t.thrown && g2++ < 200) t.tick();
        CHECK(!t.thrown && t.active,  "thrown weapon lands and rests on the ground");
    }

    // ── Bug fixes: solid() gating + throw trigger by wpoint velocity ─────────
    dat::File stone = dat::load("data/weapon1.dat");   // heavy (type 2)
    if (!stone.frames.empty()) {
        lf2::Object s;
        s.spawn(&stone, 0, 1000.f, 365.f, 365.f, true, lf2::weapon_frame::HEAVY_ON_GROUND, 0);
        s.weaponType = 2;
        s.restOnGround(1000.f, 365.f, 365.f, true);
        CHECK(s.solid(), "heavy weapon RESTING on the ground is solid (body-blocks)");
        s.held = true;
        CHECK(!s.solid(), "heavy weapon in hand is NOT solid (no self-shove)");
        s.held = false; s.groundY = 500.f;
        s.throwFrom(1000.f, 400.f, 400.f, true, 9.f, -4.f, 2.f);
        CHECK(s.thrown && !s.solid(),
              "thrown heavy weapon is NOT solid mid-flight (bug 1: holder shove)");
    }

    // Throw trigger is data-driven: the release is a HOLD wpoint (kind 1) that
    // carries a velocity. Confirm dennis's throw frames expose exactly that, so
    // main.cpp's velocity-based throw (not the non-existent kind 3) is correct.
    {
        dat::File dennis = dat::load("data/dennis.dat");
        if (!dennis.frames.empty()) {
            auto hasThrowWp = [](const dat::Frame* fr) {
                if (!fr) return false;
                for (const auto& w : fr->wpoints)
                    if (w.kind == 1 && (w.dvx || w.dvy || w.dvz)) return true;
                return false;
            };
            CHECK(hasThrowWp(dennis.frame(47)), "dennis 47 (light throw): kind-1 wpoint carries dvx");
            CHECK(hasThrowWp(dennis.frame(51)), "dennis 51 (heavy throw): kind-1 wpoint carries dvx");
            // And the hold/attack frames must NOT (they'd throw prematurely).
            bool holdHasVel = false;
            for (const auto& w : dennis.frame(0)->wpoints)
                if (w.kind == 1 && (w.dvx || w.dvy || w.dvz)) holdHasVel = true;
            CHECK(!holdHasVel, "dennis standing hold wpoint has no velocity (won't auto-throw)");
        }
    }

    if (g_fail) { std::printf("\n%d CHECK(S) FAILED\n", g_fail); return 1; }
    std::printf("\nall object tests passed\n");
    return 0;
}
