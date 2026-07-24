#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  object.hpp — live game objects (projectiles / balls), data-driven
//
//  A ball is just a Fighter walking its own .dat (dennis_ball.dat & friends):
//  `flying` frames (state 3000) carry the flight speed in their dvx and loop
//  via next:999; on connecting the ball enters its `hiting` frames (3001) and
//  removes itself at next:1000. Spawning comes from the parent actor's opoint.
//
//  Pure C++ (dat.hpp + fighter.hpp only) — host-testable.
// ─────────────────────────────────────────────────────────────────────────────
#include "types.hpp"    // MAP_W (despawn bounds)
#include "fighter.hpp"

namespace lf2 {

// Ball frame-state categories (observed in dennis_ball.dat).
enum ObjState {
    OST_FLYING       = 3000,
    OST_HITING       = 3001,   // (sic — original spelling) just hit someone
    OST_HIT          = 3002,   // hit by another ball
    OST_REBOUNDING   = 3003,
    OST_DISAPPEARING = 3004,
};

// Canonical ball frame ids.
namespace oid_frame {
    constexpr int FLYING = 0;
    constexpr int HITING = 10;
    constexpr int HIT    = 20;
}

struct Object {
    Fighter f;
    bool  active = false;
    int   team   = 0;       // 0 = player-owned, 1 = enemy-owned (no friendly fire)
    int   sheetSlot = -1;   // renderer texture bank slot for this object's dat
    int   rehit = 0;        // re-hit timer vs victims (vrest-driven)
    int   ttl   = 0;        // safety despawn

    void spawn(const dat::File* d, int sheet, float ax, float ay, float az,
               bool facingRight, int action, int ownerTeam,
               float vx0 = 0.f, float vy0 = 0.f)
    {
        f = Fighter();
        f.load(d);
        f.facingRight = facingRight;
        f.x = ax; f.y = ay; f.z = az;
        // Launch velocity from the opoint. The flying frames' dvx then either
        // overrides it (balls carry dvx:15) or KEEPS it (arrows are dvx:0 and
        // fly purely on the opoint's dvx — without this they'd stall in place).
        f.vx = vx0; f.vy = vy0;
        f.setFrame(action > 0 ? action : oid_frame::FLYING);
        sheetSlot = sheet;
        team   = ownerTeam;
        rehit  = 0;
        ttl    = 30 * 12;            // 12 s hard cap
        active = true;
    }

    bool flying() const { return f.state() == OST_FLYING; }

    // The ball connected with someone: play the hit animation and stop moving.
    void onHit() {
        f.vx = 0; f.vy = 0;
        if (f.data && f.data->frame(oid_frame::HITING)) f.setFrame(oid_frame::HITING);
        else f.removed = true;
    }

    void tick() {
        if (!active) return;
        if (rehit > 0) --rehit;
        f.x += f.vx;                 // dvx of the flying frames = flight speed
        f.y += f.vy;
        f.advance();
        if (f.removed || --ttl <= 0 ||
            f.x < -200.f || f.x > (float)MAP_W + 200.f)
            active = false;
    }
};

// Fixed-size pool — no allocations in the game loop.
template <int N>
struct ObjectPool {
    Object objs[N];

    Object* alloc() {
        for (auto& o : objs) if (!o.active) return &o;
        return nullptr;              // pool exhausted: drop the spawn
    }
    template <typename F> void forEach(F fn) {
        for (auto& o : objs) if (o.active) fn(o);
    }
    void clear() { for (auto& o : objs) o.active = false; }
};

} // namespace lf2
