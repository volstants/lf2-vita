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
        p.prevSpc = false; p.seqStage = 0; p.right = true;
        p.tick(false, true, false, false, false, false, false, true); // F+Square
        CHECK(p.f.frameId == 235,      "special still fires with full MP");
        if (cost > 0)
            CHECK(p.f.mp <= mp0 - cost + 1, "special deducts the frame's mp cost");
        // Drain MP → special must whiff.
        lf2::Player p2; p2.load(&dennis);
        p2.f.mp = 0; p2.prevSpc = false; p2.seqStage = 0; p2.right = true;
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

    if (g_fail) { std::printf("\n%d CHECK(S) FAILED\n", g_fail); return 1; }
    std::printf("\nall object tests passed\n");
    return 0;
}
