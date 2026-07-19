#pragma once
#include "../engine/types.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Firen — frame data from firen.dat / firen.js (LF2 original)
//  Used as the AI enemy in the current build.
// ─────────────────────────────────────────────────────────────────────────────

// ── Stats ─────────────────────────────────────────────────────────────────────
constexpr int   FIREN_HP         = 500;
constexpr int   FIREN_INJURY     = 20;
constexpr float FIREN_RUN_SPEED  = 10.5f;
constexpr float FIREN_RUN_SPEEDZ = 1.65f;

// ── Body / attack hitboxes ────────────────────────────────────────────────────
static const Box FIREN_BDY    = { 25, 11, 35, 69 };
static const Box FIREN_ITR_R  = { 33, 33, 42, 16 };
static const Box FIREN_ITR_L  = {  4, 33, 42, 16 };  // mirror: 79-33-42=4
constexpr int    FIREN_ITR_SI = 1;

// ── Frame sequences ───────────────────────────────────────────────────────────
// Firen shares idle/walk/run/dash/jump sequences with Dennis (same pic layout)
static const FrameData FIREN_IDLE_SEQ[] = {
    {0,5,1}, {1,4,2}, {2,5,3}, {3,6,0}
};
static const FrameData FIREN_WALK_SEQ[] = {
    {4,3,1}, {5,3,2}, {6,3,3}, {7,3,0}
};
static const FrameData FIREN_RUN_SEQ[] = {
    {20,3,1}, {21,3,2}, {22,3,0}
};
static const FrameData FIREN_DASH_SEQ[] = {
    {63,8,1}, {64,8,0}
};
static const FrameData FIREN_JUMP_SEQ[] = {
    {60,2,0}, {61,1,1}, {62,2,2}
};
static const FrameData FIREN_ATTACK_SEQ[] = {
    {10,4,1}, {11,6,-1}
};

// Damage reactions.
// NOTE: Firen ships with only ONE sheet (firen_0.png = pics 0-69). Dennis's hit
// pics (120-124) live in his 2nd/3rd sheet, which Firen doesn't have, so using
// them drew an off-sheet (blank) cell — the enemy "vanished" when hit. These use
// the in-range tumble pics (30-31) so the reaction is visible.
static const FrameData FIREN_HIT_SEQ[] = {
    {30,4,1}, {31,4,-1}
};
static const FrameData FIREN_HIT_KD_SEQ[] = {
    {30,3,1}, {31,3,-1}
};
static const FrameData FIREN_FALL_SEQ[] = {
    {30,3,1}, {31,3,2}, {32,3,3}, {33,3,4}, {34,3,-1}
};
static const FrameData FIREN_DEAD_SEQ[] = {
    {44,99,0}
};

// ── Sequence table ────────────────────────────────────────────────────────────
// Order MUST match enum St: IDLE WALK JUMP ATTACK FALL DEAD ATTACK2 HIT HIT_KD RUN DASH
static const FrameData* FIREN_SEQS[] = {
    FIREN_IDLE_SEQ,    // St::IDLE    = 0
    FIREN_WALK_SEQ,    // St::WALK    = 1
    FIREN_JUMP_SEQ,    // St::JUMP    = 2
    FIREN_ATTACK_SEQ,  // St::ATTACK  = 3
    FIREN_FALL_SEQ,    // St::FALL    = 4
    FIREN_DEAD_SEQ,    // St::DEAD    = 5
    FIREN_IDLE_SEQ,    // St::ATTACK2 = 6  (Firen has no combo — reuses idle)
    FIREN_HIT_SEQ,     // St::HIT     = 7
    FIREN_HIT_KD_SEQ,  // St::HIT_KD  = 8
    FIREN_RUN_SEQ,     // St::RUN     = 9
    FIREN_DASH_SEQ,    // St::DASH    = 10
};
