// ─────────────────────────────────────────────────────────────────────────────
//  test_fighter — unit tests for the generic frame interpreter (fighter.hpp)
//
//  Two tiers, mirroring test_dat:
//   • Synthetic frames built in memory — always run, cover the dv semantics,
//     wait timing and dangling-next tolerance with no game files needed.
//   • Real dennis.dat — run only when data/dennis.dat is present; validates the
//     interpreter against the actual idle cycle and hitboxes.
// ─────────────────────────────────────────────────────────────────────────────
#include "engine/fighter.hpp"
#include <cstdio>
#include <cmath>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } \
    else         { std::printf("  ok  : %s\n", msg); } \
} while (0)

static bool approx(float a, float b) { return std::fabs(a - b) < 0.001f; }

// Build a tiny in-memory character: frame 0 → 1 → (dangling) and a couple of
// dv variants, so we can test semantics without the copyrighted .dat files.
static dat::File makeSynthetic() {
    dat::File f;
    auto add = [&](int id, int st, int wait, int next,
                   int dvx, int dvy, int dvz, int cx, int cy) {
        dat::Frame fr;
        fr.id = id; fr.state = st; fr.wait = wait; fr.next = next;
        fr.dvx = dvx; fr.dvy = dvy; fr.dvz = dvz;
        fr.centerx = cx; fr.centery = cy; fr.pic = id;
        f.frameIndex[id] = (int)f.frames.size();
        f.frames.push_back(fr);
    };
    // id 0: wait 2, moves to 1, no velocity change (KEEP)
    add(0, lf2::ST_STANDING, 2,   1,   lf2::DV_KEEP, lf2::DV_KEEP, lf2::DV_KEEP, 39, 79);
    // id 1: wait 0, SET vx=9 forward, then goes to a MISSING frame (777)
    add(1, lf2::ST_ROWING,   0, 777,   9,            lf2::DV_KEEP, lf2::DV_KEEP, 39, 79);
    // id 2: STOP — zero the velocity
    add(2, lf2::ST_STANDING, 3,   0,   lf2::DV_STOP, lf2::DV_STOP, lf2::DV_KEEP, 39, 79);
    // give frame 0 a body box for the mirror test
    f.frames[0].bdys.push_back({0, 28, 15, 27, 65});
    return f;
}

static void testSynthetic() {
    std::printf("[synthetic]\n");
    dat::File file = makeSynthetic();
    lf2::Fighter fi; fi.load(&file);

    CHECK(fi.frameId == 0,            "starts on standing frame 0");
    CHECK(fi.maxHp == 500,           "default HP is 500");

    // wait: N shows for N+1 ticks. Frame 0 wait:2 → advances on the 3rd tick.
    fi.advance(); CHECK(fi.frameId == 0, "wait:2 still on frame 0 after 1 tick");
    fi.advance(); CHECK(fi.frameId == 0, "wait:2 still on frame 0 after 2 ticks");
    fi.advance(); CHECK(fi.frameId == 1, "wait:2 -> next on 3rd tick (wait+1)");

    // Frame 1 SET vx=9 forward (facing right).
    CHECK(approx(fi.vx, 9.f),        "dvx set: vx = 9 facing right");

    // Frame 1 wait:0 → single tick, next=777 (missing) → tolerated to standing.
    fi.advance();
    CHECK(fi.frameId == lf2::STANDING_FRAME, "dangling next falls back to standing");

    // KEEP: re-entering frame 0 (dvx 0) must NOT touch the carried vx.
    CHECK(approx(fi.vx, 9.f),        "dvx:0 keeps current velocity");

    // Facing flip of the SET rule.
    fi.facingRight = false;
    fi.setFrame(1);
    CHECK(approx(fi.vx, -9.f),       "dvx set mirrors to -9 facing left");

    // STOP rule.
    fi.setFrame(2);
    CHECK(approx(fi.vx, 0.f) && approx(fi.vy, 0.f), "dvx/dvy 550 -> velocity zeroed");

    // Mirror math on the body box of frame 0 (centerx 39; box x28 w27).
    fi.x = 100.f; fi.y = 200.f;
    fi.facingRight = true;  fi.setFrame(0, false);
    lf2::WBox br = fi.worldBox(28, 15, 27, 65);
    CHECK(approx(br.x, 100.f + (28 - 39)),        "box facing right: x + (bx-cx)");
    fi.facingRight = false; fi.setFrame(0, false);
    lf2::WBox bl = fi.worldBox(28, 15, 27, 65);
    CHECK(approx(bl.x, 100.f + (39 - 28 - 27)),   "box facing left mirrors around center");
    CHECK(approx(br.y, bl.y),                     "box Y does not mirror");
}

static void testDennis() {
    dat::File dennis = dat::load("data/dennis.dat");
    if (dennis.frames.empty()) {
        std::printf("[dennis] data/dennis.dat not present — skipping\n");
        return;
    }
    std::printf("[dennis] %zu frames\n", dennis.frames.size());
    lf2::Fighter fi; fi.load(&dennis);

    const dat::Frame* f0 = fi.cur();
    CHECK(f0 && f0->state == lf2::ST_STANDING, "frame 0 is a standing frame");
    CHECK(f0 && f0->wait == 5 && f0->next == 1, "frame 0: wait 5, next 1");

    // Walk the idle cycle 0->1->2->3->(999)->0 and confirm it returns home.
    int guard = 0; bool looped = false;
    fi.setFrame(0);
    while (guard++ < 200) {
        fi.advance();
        if (fi.frameId == 0 && guard > 4) { looped = true; break; }
    }
    CHECK(looped, "idle cycle returns to standing via next:999");

    // Rowing frame 102 carries dv(9,0,0): entering it sets forward velocity.
    if (dennis.frame(102)) {
        fi.facingRight = true;  fi.setFrame(102);
        CHECK(approx(fi.vx, 9.f),  "dennis frame 102 (rowing) sets vx=9 right");
        fi.facingRight = false; fi.setFrame(102);
        CHECK(approx(fi.vx, -9.f), "dennis frame 102 mirrors to vx=-9 left");
    }

    // The current pic must resolve to one of the 3 declared sheets.
    fi.setFrame(0);
    int sx, sy, sw, sh;
    CHECK(fi.srcRect(sx, sy, sw, sh) && sw == 79 && sh == 79,
          "srcRect resolves pic 0 to a 79x79 cell");
}

int main() {
    testSynthetic();
    testDennis();
    if (g_fail) { std::printf("\n%d CHECK(S) FAILED\n", g_fail); return 1; }
    std::printf("\nall fighter tests passed\n");
    return 0;
}
