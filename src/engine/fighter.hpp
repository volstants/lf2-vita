#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  fighter.hpp — generic LF2 frame interpreter
//
//  Replaces the hand-transcribed per-character state machines (dennis.hpp /
//  firen.hpp with their invented `St` enum) by a runtime that WALKS the frame
//  graph of a parsed .dat file directly. Adding a character becomes "point the
//  interpreter at woody.dat" instead of writing woody.hpp.
//
//  Pure C++ (only depends on dat.hpp) so it compiles and is unit-tested on the
//  host, with no SDL / Vita headers. Rendering and input wiring live elsewhere.
//
//  Coordinate model (self-contained; main.cpp maps its x/z/h into this later):
//    x  world X of the character's anchor (the sprite's centerx column)
//    y  world Y of the anchor            (the sprite's centery row)
//    z  ground depth (parallax / hit tolerance axis)
//  Sub-boxes (bdy/itr/…) are stored in the .dat relative to the sprite's
//  top-left; worldBox() rebases them onto (x,y) and mirrors around centerx
//  when facing left — the canonical LF2 flip.
// ─────────────────────────────────────────────────────────────────────────────
#include "dat.hpp"

namespace lf2 {

// ── Frame-graph sentinels (observed in the original data) ────────────────────
constexpr int NEXT_STANDING  = 999;   // `next: 999` → return to standing frame
constexpr int NEXT_REMOVE    = 1000;  // `next: 1000` → remove the object (balls)
constexpr int NEXT_DISAPPEAR = 1280;  // `next: 1280` → vanish (Rudolf's disappear)
constexpr int STANDING_FRAME = 0;     // characters' standing frame id

// Frame dv* semantics (as told apart in the real files):
//   dv == 0    → KEEP the current velocity component (momentum carries)
//   dv == 550  → STOP: set that velocity component to 0
//   otherwise  → SET velocity to dv (dvx/dvz are applied in the facing dir)
constexpr int DV_KEEP = 0;
constexpr int DV_STOP = 550;

// ── LF2 `state` categories the engine keys behaviour on ──────────────────────
// (Only a subset drives special handling; the rest animate generically.)
enum State {
    ST_STANDING   = 0,
    ST_WALKING    = 1,
    ST_RUNNING    = 2,
    ST_ATTACK     = 3,
    ST_JUMP       = 4,
    ST_DASH       = 5,
    ST_ROWING     = 6,   // post-hit air drift
    ST_DEFEND     = 7,
    ST_BROKEN_DEF = 8,
    ST_CATCHING   = 9,
    ST_CAUGHT     = 10,
    ST_INJURED    = 11,
    ST_FALLING    = 12,
    ST_ICE        = 13,
    ST_LYING      = 14,
    ST_SPECIAL    = 15,  // throws / super moves / misc scripted frames
    ST_BURNING    = 18,  // on fire (frames 203-206). Loops in the data; the exit
                         // is external (F.LF locks it for 36 TU, then the victim
                         // collapses — state 18 delegates to falling on landing).
    ST_INJURED2   = 16,  // Dance of Pain (frames 226-229): the long stun, and the
                         // ONLY state an itr kind 1 (catch_injured) may grab
                         // (OpenLF2 const.c:102 injured_state_2 = 16).
    ST_BURN_RUN   = 19,  // Firen's fire run (firen.dat 255-261). Immune to fire,
                         // exactly like ST_BURNING — see itrEffectAllows().
};

// ── itr `effect` gate ────────────────────────────────────────────────────────
// SOURCE: lf2.exe, FUN_00417400 @ 0x00417400 ("does attack succeed"), the
// early-out chain that runs for `itr->kind == 0` before any box test. In the
// decompilation the itr is `piVar8` (stride 0x50) and `piVar8[0xb]` is
// itr->effect (+0x2c); the victim's state is read as
// frames[frame_id3].state (+0x7ac), the file type as file->type (+0x6f8):
//
//   effect 4  (shrafe)  && victim is a character                → NO HIT
//   effect 20 (burn)    && (victim NOT a character || state 18 || 19) → NO HIT
//   effect 21 (flame)   && (victim state 18 || state 19)        → NO HIT
//   effect 30 (column)  && victim frame id in 200..202 (frozen) → NO HIT
//   effect 2  (fire)    && ATTACKER state 19 && victim state 18 → NO HIT
//
// NOTE ON THE HIERARCHY: OpenLF2's class_global.c:52-73 has the same five
// rules but labels the first two `fire_effect (2)` and `burn_effect (20)`.
// The binary uses 0x14 (20) and 0x15 (21) there. The binary wins.
//
// This is what makes fire self-consistent in the original: a character that is
// already burning (state 18) or doing the fire run (state 19) cannot be burnt
// again, and cannot be hurt by the flames that a burning body radiates.
inline bool itrEffectAllows(int effect, int attackerState, int victimState,
                            int victimFrameId, bool victimIsCharacter)
{
    if (effect == 4  && victimIsCharacter) return false;
    if (effect == 20 && (!victimIsCharacter ||
                         victimState == ST_BURNING || victimState == ST_BURN_RUN))
        return false;
    if (effect == 21 && (victimState == ST_BURNING || victimState == ST_BURN_RUN))
        return false;
    if (effect == 30 && victimFrameId >= 200 && victimFrameId <= 202) return false;
    if (effect == 2  && attackerState == ST_BURN_RUN && victimState == ST_BURNING)
        return false;
    return true;
}

// ── Os dois temporizadores de re-acerto ──────────────────────────────────────
// SOURCE: lf2.exe 0x0042f2c8-0x0042f31b, com o irmão em 0x0043030f-0x004303ce.
// Com `ecx` = itr, `ebx` = id do atacante, `edi` = id da vítima:
//
//     42f2d6:  mov  0x20(%ecx),%eax      ; itr->arest   (itr+0x20)
//     42f2d9:  cmp  $0x4,%eax
//     42f2dc:  jge  0x42f2f7
//     42f2de:  cmpl $0x0,0x24(%ecx)      ; itr->vrest   (itr+0x24)
//     42f2e2:  jne  0x42f2f7
//     42f2e4:  movl $0x4,0xec(%eax)      ; atacante->arest = 4     (PISO)
//     42f2f7:  mov  %eax,0xec(%edx)      ; atacante->arest = itr->arest
//     42f304:  cmpl $0x0,0x24(%ecx)
//     42f308:  jle  0x42f31b             ; vrest <= 0 → nada por par
//     42f314:  mov  %cl,0xf0(%eax,%ebx,1); vitima->vrest_of_objects[atacante]
//
// Consequências que o engine tinha erradas ao fundir os dois num só valor:
//   • `arest` é gravado em TODO acerto, inclusive quando o itr também tem vrest;
//   • o piso é 4, e só quando `arest < 4` E `vrest == 0` (o 8/9 era invenção);
//   • `vrest` é BYTE, por par, e só é gravado quando > 0.
inline void applyRest(int itrArest, int itrVrest, int& attackerArest, int& pairVrest) {
    int a = itrArest;
    if (a < 4 && itrVrest == 0) a = 4;
    attackerArest = a;
    if (itrVrest > 0) pairVrest = itrVrest;
}

// World-space axis-aligned box (float so it composes with sub-pixel motion).
struct WBox { float x = 0, y = 0, w = 0, h = 0; };

inline bool overlap(const WBox& a, const WBox& b) {
    return a.w > 0 && b.w > 0 && a.h > 0 && b.h > 0
        && a.x < b.x + b.w && a.x + a.w > b.x
        && a.y < b.y + b.h && a.y + a.h > b.y;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fighter — one live actor driven by a dat::File
// ─────────────────────────────────────────────────────────────────────────────
struct Fighter {
    const dat::File* data = nullptr;

    // Position / motion (see coordinate model above).
    float x = 0.f, y = 0.f, z = 0.f;
    float vx = 0.f, vy = 0.f, vz = 0.f;
    bool  facingRight = true;

    // Frame-graph cursor.
    int   frameId = STANDING_FRAME;
    int   timer   = 0;    // ticks elapsed on the current frame (see advance())
    bool  removed = false; // hit a `next: 1000` — objects despawn, actors reset
    bool  vanishReq = false; // hit a `next: 1280` — the owner should turn invisible
    bool  flipReq   = false; // hit a negative `next` — the owner should turn around.
                             // A character's facing lives on the controller and is
                             // pushed into the Fighter by syncAnchor() every tick,
                             // so flipping only facingRight here would be undone.

    // ── Frame dvy, staged for an owner that integrates height itself ─────────
    // Free objects fly on Fighter::vy directly, but a CHARACTER's vertical state
    // lives on the controller (Player::h / Player::vy) — so a frame's dvy written
    // only into Fighter::vy is read by nobody and the value sits here forever.
    // That was the case for every dvy in the character data: frame 202, the last
    // ice frame, carries dvy -3 in all 24 character files and it never moved
    // anyone. Frames stage their dvy here and Player::drainFrameDvy() pulls it in.
    float dvyPending = 0.f;   // accumulated dvy since the last drain (neg = up)
    bool  dvyStop    = false; // a 550 landed: ZERO the velocity, don't add to it

    // Vitals (LF2 characters are all 500 HP/MP unless a file overrides them).
    int   hp = 0, mp = 0, maxHp = 0, maxMp = 0;

    // ── Setup ────────────────────────────────────────────────────────────────
    void load(const dat::File* d) {
        data  = d;
        maxHp = hp = (int)d->header.get("hp", 500.f);
        maxMp = (int)d->header.get("mp", 500.f);
        mp    = 200;    // F.LF global.js: mp_start = 200 (not full), never overridden
        removed = false;
        setFrame(STANDING_FRAME, /*applyDv=*/false);
    }

    const dat::Frame* cur() const { return data ? data->frame(frameId) : nullptr; }
    int   state()          const { const dat::Frame* f = cur(); return f ? f->state : ST_STANDING; }
    bool  is(int s)        const { return state() == s; }

    // ── Velocity rule (0 keep / 550 stop / else set) ─────────────────────────
    void applyDvx(int dv) {
        if (dv == DV_KEEP) return;
        if (dv == DV_STOP) { vx = 0.f; return; }
        vx = facingRight ? (float)dv : -(float)dv;
    }
    void applyDvy(int dv) {
        if (dv == DV_KEEP) return;
        if (dv == DV_STOP) { vy = 0.f; dvyPending = 0.f; dvyStop = true; return; }
        // F.LF frame_force(): dvy ACCUMULATES (vy += dvy) while dvx/dvz SET.
        vy += (float)dv;                      // negative = upward, no facing flip
        dvyPending += (float)dv;              // …and stage it for a Player owner
    }
    void applyDvz(int dv) {
        if (dv == DV_KEEP) return;
        if (dv == DV_STOP) { vz = 0.f; return; }
        vz = (float)dv;
    }

    // ── Frame transitions ────────────────────────────────────────────────────
    // Enter a frame by id. Missing ids fall back to standing (the original data
    // contains a handful of dangling `next` targets — see datdump --validate).
    void setFrame(int id, bool applyDv = true) {
        frameId = id;
        const dat::Frame* f = cur();
        if (!f) { frameId = STANDING_FRAME; f = cur(); }
        timer = 0;
        if (f && applyDv) {
            applyDvx(f->dvx);
            applyDvy(f->dvy);
            applyDvz(f->dvz);
        }
    }

    // Follow the current frame's `next` link.
    //   999   → standing
    //   1000  → removed (objects despawn)
    //   1280  → "disappear": go to standing AND vanish (Rudolf frame 257). The
    //           owner turns invisible and untouchable for a while; F.LF hides the
    //           sprite and the shadow and sets effect.super, clearing it ~150 TU
    //           later with a blink.
    //   < 0   → go to |next| and FLIP the facing afterwards (F.LF
    //           switch_dir_after_trans). Louis's c-throw (frame 270, next -999)
    //           turns him around as he hurls the victim behind him.
    //   dangling → standing
    void gotoNext() {
        const dat::Frame* f = cur();
        if (!f) { setFrame(STANDING_FRAME); return; }
        int nx = f->next;
        // `next: 0` means HOLD THIS FRAME — not "go to frame 0". F.LF is explicit
        // about it (`if (next === 0) { // do nothing }` in livingobject.trans).
        // We were jumping to frame 0, which is STANDING, so anything ending on a
        // next:0 frame popped upright: the falling frames 180-186 are all next:0,
        // and that is why a burnt or frozen victim never stayed down. Whoever owns
        // the state (the landing code, cycleAnim…) decides when to leave.
        if (nx == 0) return;
        bool flip = false;
        if (nx < 0) { flip = true; nx = -nx; }
        if (nx == NEXT_REMOVE) { removed = true; return; }
        if (nx == NEXT_DISAPPEAR) { vanishReq = true; nx = NEXT_STANDING; }
        if (nx == NEXT_STANDING) setFrame(STANDING_FRAME);
        else                     setFrame(nx);   // tolerates a missing id
        if (flip) { facingRight = !facingRight; flipReq = true; }
    }

    // Advance one 30 Hz logic tick.
    //
    // LF2 wait semantics: a frame with `wait: N` is shown for N+1 ticks. The
    // timer counts up and, once it exceeds `wait`, the frame yields to `next`.
    // (So wait:0 shows for a single tick.) Run this at 30 Hz — NOT at the 60 Hz
    // render rate — or every animation plays at double speed.
    void advance() {
        const dat::Frame* f = cur();
        if (!f) return;
        if (++timer > f->wait) gotoNext();
    }

    // ── Input-driven jumps (first cut) ───────────────────────────────────────
    // Maps the current frame's hit_* table to a frame jump. `forward` means the
    // pressed direction matches the facing. Combo timing windows and the
    // double-tap (dash/run) detection are handled by the caller / a later pass;
    // this only resolves the immediate single-input branches present per frame.
    bool tryInput(bool atk, bool jmp, bool def, bool forward, bool up, bool down) {
        const dat::Frame* f = cur();
        if (!f) return false;
        auto go = [&](int id) { if (id > 0) { setFrame(id); return true; } return false; };

        if (atk) {
            if (forward && go(f->hit_Fa)) return true;
            if (up      && go(f->hit_Ua)) return true;
            if (down    && go(f->hit_Da)) return true;
            if (go(f->hit_a)) return true;
        }
        if (jmp) {
            if (forward && go(f->hit_Fj)) return true;
            if (up      && go(f->hit_Uj)) return true;
            if (down    && go(f->hit_Dj)) return true;
            if (go(f->hit_j)) return true;
        }
        if (def && go(f->hit_d)) return true;
        return false;
    }

    // ── Rendering helpers ────────────────────────────────────────────────────
    int pic() const { const dat::Frame* f = cur(); return f ? f->pic : 0; }

    // Sheet + source rect for the current pic. Uses the header's declared sheet
    // ranges (row/col trap handled inside dat::SpriteSheet::pixelOf).
    const dat::SpriteSheet* sheet() const {
        return data ? data->header.sheetOf(pic()) : nullptr;
    }
    bool srcRect(int& sx, int& sy, int& sw, int& sh) const {
        const dat::SpriteSheet* s = sheet();
        if (!s) return false;
        s->pixelOf(pic(), sx, sy);
        sw = s->w; sh = s->h;
        return true;
    }

    // Sheet ORDINAL (0,1,2… in header declaration order) + the pic's local index
    // within it. The render layer maps the ordinal to a loaded texture and lays
    // the cell out on the PNG export's own grid. NOTE: the shipped LF2 PNGs are
    // exported on an 80 px grid (800-wide = 10×80), even though the .dat declares
    // 79 px cells — so the renderer must stride by the texture grid, not by
    // SpriteSheet::pixelOf(). This is what fixes the old pic>=100 boundary bug
    // (real sheet splits are 0-69 / 70-139 / 140-209).
    bool sheetLocal(int& ordinal, int& local) const {
        if (!data) return false;
        int p = pic();
        for (size_t i = 0; i < data->header.files.size(); ++i)
            if (data->header.files[i].contains(p)) {
                ordinal = (int)i;
                local   = p - data->header.files[i].startPic;
                return true;
            }
        return false;
    }

    // Top-left where the sprite should be blitted, given the anchor at (x,y).
    // Facing left, the renderer mirrors the sprite, so the origin shifts by the
    // mirrored center: drawX = x - (sheetW - centerx).
    void drawOrigin(float& dx, float& dy) const {
        const dat::Frame* f = cur();
        int cx = f ? f->centerx : 0, cy = f ? f->centery : 0;
        const dat::SpriteSheet* s = sheet();
        int sw = s ? s->w : 0;
        dx = facingRight ? (x - cx) : (x - (sw - cx));
        dy = y - cy;
    }

    // ── Collision boxes (world space, facing-mirrored) ───────────────────────
    // A .dat box is relative to the sprite top-left. Facing right it rebases to
    // x + (bx - centerx); facing left it mirrors around centerx to
    // x + (centerx - bx - bw). Y never mirrors.
    WBox worldBox(int bx, int by, int bw, int bh) const {
        const dat::Frame* f = cur();
        int cx = f ? f->centerx : 0, cy = f ? f->centery : 0;
        float wx = facingRight ? (x + (bx - cx)) : (x + (cx - bx - bw));
        float wy = y + (by - cy);
        return { wx, wy, (float)bw, (float)bh };
    }

    // A single sprite-relative point (opoint/wpoint) in world space.
    // Sprite-space offset (px - centerx) applies as-is facing right, mirrored
    // facing left — same convention as worldBox. Verified against both an
    // opoint (fireball x:95, cx:41 → spawns in FRONT) and a wpoint.
    void pointWorld(int px, int py, float& wx, float& wy) const {
        const dat::Frame* f = cur();
        int cx = f ? f->centerx : 0, cy = f ? f->centery : 0;
        wx = facingRight ? (x + (px - cx)) : (x - (px - cx));
        wy = y + (py - cy);
    }

    // Convenience: emit all body / attack boxes of the current frame.
    template <typename F> void forEachBdy(F fn) const {
        const dat::Frame* f = cur();
        if (!f) return;
        for (const dat::Bdy& b : f->bdys) fn(worldBox(b.x, b.y, b.w, b.h), b);
    }
    template <typename F> void forEachItr(F fn) const {
        const dat::Frame* f = cur();
        if (!f) return;
        for (const dat::Itr& it : f->itrs) fn(worldBox(it.x, it.y, it.w, it.h), it);
    }
};

} // namespace lf2
