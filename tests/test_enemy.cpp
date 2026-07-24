// ─────────────────────────────────────────────────────────────────────────────
//  test_enemy — the AI-driven Enemy (lf2::Player + pursuit AI).
//  Uses dennis.dat as the actor (has all frames); skips if absent.
// ─────────────────────────────────────────────────────────────────────────────
#include "characters/enemy.hpp"
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } \
    else         { std::printf("  ok  : %s\n", msg); } \
} while (0)

int main() {
    dat::File d = dat::load("data/dennis.dat");
    if (d.frames.empty()) { std::printf("[enemy] data/dennis.dat absent — skipping\n"); return 0; }

    Enemy e; e.load(&d);
    e.a.x = 1000.f; e.a.z = 400.f;
    const float pz = 400.f;

    CHECK(e.a.state() == lf2::ST_STANDING, "enemy starts standing");
    CHECK(!e.a.right,                      "enemy defaults to facing left");

    // Player far to the left → enemy should pursue (x decreasing) and face it.
    float x0 = e.a.x;
    for (int i = 0; i < 10; ++i) e.tick(200.f, pz);
    CHECK(e.a.x < x0,  "enemy walks toward the player");
    CHECK(!e.a.right,  "enemy faces the player while chasing left");

    // Player brought into range → enemy attacks.
    e.aiCooldown = 0;
    bool attacked = false;
    for (int i = 0; i < 20 && !attacked; ++i) {
        e.tick(e.a.x - 50.f, e.a.z);
        if (e.a.state() == lf2::ST_ATTACK) attacked = true;
    }
    CHECK(attacked, "enemy attacks when the player is in range");

    // Enemy takes a heavy hit → loses HP and is knocked down.
    int hp0 = e.a.hp();
    e.a.hit(30, -10.f, /*fall=*/70);   // >60 → knockdown (fall-60 alone only staggers)
    CHECK(e.a.hp() < hp0, "enemy loses HP when hit");
    CHECK(e.a.state() == lf2::ST_FALLING || e.a.knockedDown,
          "heavy hit knocks the enemy down");

    if (g_fail) { std::printf("\n%d CHECK(S) FAILED\n", g_fail); return 1; }
    std::printf("\nall enemy tests passed\n");
    return 0;
}
