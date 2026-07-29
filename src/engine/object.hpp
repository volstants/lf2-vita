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
    // 3005 = in flight but inert to other balls (no ball-vs-ball reaction). It
    // still carries attack itrs: henry_wind.dat (Henry's AND Louis's wind, oid
    // 204) flies entirely in 3005, so treating only 3000 as "flying" made the
    // wind pass through everyone without ever connecting.
    OST_FLYING_INERT = 3005,
    // 9999 = pure decoration (broken_weapon.dat's shards, etc.dat). Its chain
    // ends on `next: 999`, which loops back to the first frame — fine for a ball
    // in flight, but an effect must play once and go, or the debris sits there.
    OST_EFFECT       = 9999,
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
    // FINAL resting frames — light 64 (state 1004), heavy 20 (state 2004),
    // per F.LF weapon.js. 60 is only the FIRST frame of the light landing chain
    // (60→64, state 1003); resting there left the weapon in a mid-fall pose a few
    // px off the floor, because tick() early-returns for a weapon at rest and the
    // frame graph never walked to 64.
    constexpr int ON_GROUND       = 64;   // light, on_ground (1004)
    constexpr int HEAVY_ON_GROUND = 20;   // heavy, on_ground (2004)
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
    int   loopGuard = -1;   // highest frame id seen, for one-shot effects
    bool  ephemeral = false;// spawned by an opoint (arrow/shuriken): expires once
                            // it has come to rest. Stage weapons are permanent.

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
        // Weapons carry their own durability: <weapon_hp> (stone 800, knife 200).
        // Fighter::load only knows the character key "hp", so a weapon would
        // otherwise start with the 500 default and never break on schedule.
        float whp = d ? d->header.get("weapon_hp", 0.f) : 0.f;
        if (whp > 0.f) f.maxHp = f.hp = (int)whp;
        sheetSlot = sheet;
        team   = ownerTeam;
        rehit  = 0;
        ttl    = 30 * 12;            // 12 s hard cap
        active = true;
        // FULL reset — the pool reuses slots, and leaving these set made a slot
        // that once held an arrow (weaponType 1) come back as smoke that still
        // "was a weapon": the weapon branch of tick() returns before animating, so
        // it froze on screen forever AND could be picked up and thrown.
        weaponType = 0;
        held       = false;
        thrown     = false;
        vz         = 0.f;
        groundY    = az;
        loopGuard  = -1;
        ephemeral  = false;
    }

    bool flying() const {
        int s = f.state();
        return s == OST_FLYING || s == OST_FLYING_INERT;
    }

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
        // No depth drift on a throw: F.LF's weapon.drop() sets only vx/vy and
        // forces ps.zz = 0. Feeding the wpoint's dvz (3 light / 2 heavy) in here
        // made the weapon slide sideways as it fell.
        (void)vz0;
        f.vx = right ? vx0 : -vx0; f.vy = vy0; vz = 0.f;
        int tf = (weaponType >= 2) ? weapon_frame::IN_SKY : 40;
        if (!(f.data && f.data->frame(tf))) tf = weapon_frame::IN_SKY;
        f.setFrame(tf);
        held = false; thrown = true; rehit = 0;
        ttl = 30 * 12;
    }

    // Released without a throw impulse: fall under gravity until it reaches the
    // floor line, then rest. (restOnGround alone would freeze it mid-air — a
    // weapon dropped while jumping/hit just hung there.)
    void dropAt(float ax, float ay, float az, bool right, float floorY) {
        f.facingRight = right;
        f.x = ax; f.y = ay; f.z = az;
        f.vx = 0.f; f.vy = 0.f; vz = 0.f;
        groundY = az;                  // floor line == depth
        held = false;
        (void)floorY;
        if (ay >= az) { restOnGround(ax, az, az, right); return; }
        int df = (weaponType >= 2) ? weapon_frame::IN_SKY : 40;
        if (!(f.data && f.data->frame(df))) df = weapon_frame::IN_SKY;
        f.setFrame(df);
        thrown = true;                 // reuse the airborne path (gravity + landing)
        rehit  = 0;
        ttl    = 30 * 12;
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

    // Struck by an attack. Weapons lose <weapon_hp> durability and break at 0
    // (F.LF weapon.js 'die' → frame 1000 + broken effect + weapon_broken_sound).
    // A hit also kicks the weapon: F.LF gives a heavy one soft_bounceup.vy = -2
    // and a light one weapon.hit.vy, so it reacts instead of sitting there.
    // Returns true when this hit broke it.
    bool takeHit(int injury, bool fromRight) {
        if (weaponType <= 0) return false;
        f.hp -= (injury > 0 ? injury : 10);
        f.vx  = fromRight ? -3.f : 3.f;
        if (!held) {
            f.vy   = (weaponType >= 2) ? -2.f : -3.f;   // soft bounce up
            thrown = true;                              // rejoin the airborne path
        }
        return f.hp <= 0;
    }

    // NOTE: there is deliberately no solid()/body-block for weapons. LF2 lets
    // fighters walk straight over a weapon lying on the ground; the only blocker
    // in the engine is an itr of kind 14, which slows movement rather than
    // displacing the fighter (F.LF mechanics.blocking_xz).

    void tick() {
        if (!active) return;
        if (held) return;                        // holder drives position + frame
        if (rehit > 0) --rehit;
        if (weaponType > 0) {
            if (!thrown) {
                // At rest. A weapon that belongs to the stage stays put forever,
                // but one fired from an opoint (Henry's arrows, Rudolf's shuriken)
                // must expire: this branch returns before the ttl countdown, so
                // every landed arrow used to sit in the pool permanently until the
                // 24 slots were gone and specials stopped spawning anything.
                if (ephemeral && --ttl <= 0) active = false;
                return;
            }
            // In flight: gravity, spin through the sky frames, land at groundY.
            f.vy += WEAPON_FLY_GRAVITY;
            f.x += f.vx; f.y += f.vy; f.z += vz;
            f.advance();
            // The floor line for a given depth IS z (that's where a fighter's feet
            // are). Using the groundY captured at throw time left the weapon
            // resting in mid-air — at head height — whenever the thrower's z had
            // moved or the throw drifted in z (heavy throw carries dvz).
            float floorY = f.z;
            if (f.y >= floorY) {                 // fell onto the ground
                f.y = floorY;
                thrown = false;
                restOnGround(f.x, floorY, f.z, f.facingRight);
            }
            if (f.x < -200.f || f.x > (float)MAP_W + 200.f) active = false;
            return;
        }
        f.x += f.vx;                             // projectile: dvx = flight speed
        f.y += f.vy;
        int beforeId = f.frameId;
        f.advance();
        // One-shot effects: the moment the chain wraps to an earlier frame the
        // animation has played through, so retire it instead of looping forever.
        if (f.state() == OST_EFFECT) {
            if (loopGuard >= 0 && f.frameId < beforeId) { active = false; return; }
            loopGuard = beforeId;
        }
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
