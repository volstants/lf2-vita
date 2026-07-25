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
// Weapon frame states: light weapons use 1000-1004, heavy ones 2000-2004.
namespace weapon_state {
    constexpr int LIGHT_ON_GROUND = 1004, HEAVY_ON_GROUND = 2004;
    constexpr int LIGHT_ON_HAND   = 1001, HEAVY_ON_HAND   = 2001;
}
namespace weapon_frame {
    constexpr int ON_GROUND       = 60;   // light: 60-64 (weapon4) · heavy: 20 (weapon1)
    constexpr int HEAVY_ON_GROUND = 20;
    constexpr int IN_SKY          = 0;
}

struct Object {
    Fighter f;
    bool  active = false;
    int   team   = 0;       // 0 = player-owned, 1 = enemy-owned (no friendly fire)
    int   sheetSlot = -1;   // renderer texture bank slot for this object's dat
    int   rehit = 0;        // re-hit timer vs victims (vrest-driven)
    int   ttl   = 0;        // safety despawn
    int   weaponType = 0;   // 0 = projectile; 1 = light weapon; 2 = heavy weapon
    bool  held = false;     // in a holder's hand — the holder drives position/frame
    bool  thrown = false;   // airborne after a throw: flies, hits, then lands
    float vz = 0.f;         // depth velocity (throws can drift in z)
    float groundY = 0.f;    // y of the floor line it should land on

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

    // Connected with someone. A ball plays its hit animation and stops; a thrown
    // weapon bounces back a little and keeps falling (F.LF: weapon.hit.vx = -3).
    void onHit() {
        if (weaponType > 0) {
            f.vx = (f.vx > 0.f ? -3.f : 3.f);
            f.vy = 0.f;
            return;
        }
        f.vx = 0; f.vy = 0;
        if (f.data && f.data->frame(oid_frame::HITING)) f.setFrame(oid_frame::HITING);
        else f.removed = true;
    }

    // Thrown: fly with the wpoint's velocity, spinning through the in_the_sky
    // frames. F.LF weapon.act(): the holder's release impulse places it ahead
    // (light 58/-15, heavy 48/-40) and it enters frame 40 (light) / 999 (heavy).
    void throwFrom(float ax, float ay, float az, bool right, float vx0, float vy0, float vz0) {
        f.facingRight = right;
        f.x = ax + (right ? (weaponType >= 2 ? 48.f : 58.f) : -(weaponType >= 2 ? 48.f : 58.f));
        f.y = ay + (weaponType >= 2 ? -40.f : -15.f);
        f.z = az;
        f.vx = right ? vx0 : -vx0; f.vy = vy0; vz = vz0;
        int tf = (weaponType >= 2) ? weapon_frame::IN_SKY : 40;
        if (!(f.data && f.data->frame(tf))) tf = weapon_frame::IN_SKY;
        f.setFrame(tf);
        held = false; thrown = true; rehit = 0;
        ttl = 30 * 12;
    }

    // A grounded weapon: rest at its on_ground frame (differs light vs heavy).
    void restOnGround(float ax, float ay, float az, bool right) {
        f.facingRight = right;
        f.x = ax; f.y = ay; f.z = az; f.vx = f.vy = 0.f;
        int gf = (weaponType >= 2) ? weapon_frame::HEAVY_ON_GROUND : weapon_frame::ON_GROUND;
        if (!(f.data && f.data->frame(gf))) gf = weapon_frame::ON_GROUND;
        f.setFrame(gf);
        held = false;
    }

    // Solid footprint: a heavy weapon (stone, box) blocks movement in LF2 only
    // while RESTING on the ground — not in a hand, and not mid-flight after a
    // throw (a thrown weapon is a projectile, so it must not body-block the
    // thrower, which otherwise reads as "the holder gets shoved sideways").
    bool solid() const { return weaponType >= 2 && !held && !thrown; }

    void tick() {
        if (!active) return;
        if (held) return;                        // holder drives position + frame
        if (rehit > 0) --rehit;
        if (weaponType > 0) {
            if (!thrown) return;                 // resting on the ground
            // In flight: gravity, spin through the sky frames, land at groundY.
            f.vy += GRAVITY;
            f.x += f.vx; f.y += f.vy; f.z += vz;
            f.advance();
            if (f.y >= groundY) {                // fell onto the ground
                f.y = groundY;
                thrown = false;
                restOnGround(f.x, groundY, f.z, f.facingRight);
            }
            if (f.x < -200.f || f.x > (float)MAP_W + 200.f) active = false;
            return;
        }
        f.x += f.vx;                             // projectile: dvx = flight speed
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
    static constexpr int SIZE = N;

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
