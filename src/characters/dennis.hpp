#pragma once
#include "../engine/types.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Dennis — frame data from dennis.dat / dennis.js (LF2 original)
//  All hitbox coordinates are at original sprite scale (×1).
// ─────────────────────────────────────────────────────────────────────────────

// ── Stats ─────────────────────────────────────────────────────────────────────
constexpr int   DENNIS_HP         = 500;
constexpr int   DENNIS_INJURY     = 30;    // damage per punch hit
constexpr float DENNIS_RUN_SPEED  = 10.5f;
constexpr float DENNIS_RUN_SPEEDZ = 1.65f;

// ── Body / attack hitboxes (dennis.dat) ───────────────────────────────────────
// BDY  : persistent body box — receives hits
// ITR_R: attack box facing right  (left = mirror: 79 - x - w)
static const Box DENNIS_BDY    = {  19,  9, 36, 71 };

// Punch 1 — frame 11 (ATTACK_SEQ[1])
static const Box DENNIS_ITR_R  = {  21, 30, 54, 23 };
static const Box DENNIS_ITR_L  = {   4, 30, 54, 23 };
constexpr int    DENNIS_ITR_SI = 1;    // si index within ATTACK_SEQ that is "active"

// Punch 2 — frame 66 (ATTACK2_SEQ[1])
static const Box DENNIS_ITR2_R = {  17, 37, 60, 19 };
static const Box DENNIS_ITR2_L = {   2, 37, 60, 19 };
constexpr int    DENNIS_ITR2_SI = 1;

// ── Frame sequences ───────────────────────────────────────────────────────────
// {pic, wait_ticks, next_si}   next_si = -1 means "state finished"

// Idle / locomotion
static const FrameData DENNIS_IDLE_SEQ[] = {
    {0,5,1}, {1,4,2}, {2,5,3}, {3,6,0}
};
static const FrameData DENNIS_WALK_SEQ[] = {
    {4,3,1}, {5,3,2}, {6,3,3}, {7,3,0}
};
static const FrameData DENNIS_RUN_SEQ[] = {
    {20,3,1}, {21,3,2}, {22,3,0}
};
static const FrameData DENNIS_DASH_SEQ[] = {
    {63,8,1}, {64,8,0}
};
static const FrameData DENNIS_JUMP_SEQ[] = {
    {60,2,0}, {61,1,1}, {62,2,2}
};

// Attacks
static const FrameData DENNIS_ATTACK_SEQ[] = {
    {10,2,1}, {11,2,2}, {12,2,3}, {13,3,-1}
};
static const FrameData DENNIS_ATTACK2_SEQ[] = {
    {14,2,1}, {15,1,2}, {16,1,3}, {17,1,-1}
};

// Damage reactions
// HIT    → light stagger (pics 120-121, dennis_1.png)
// HIT_KD → heavy knockdown: brief stagger then transitions to FALL
// FALL   → tumble (pics 30-34); engine mirrors on X for direction
// DEAD   → lying flat (pic 44); loops indefinitely
static const FrameData DENNIS_HIT_SEQ[] = {
    {120,4,1}, {121,4,-1}      // -1 → back to IDLE
};
static const FrameData DENNIS_HIT_KD_SEQ[] = {
    {123,3,1}, {124,3,-1}      // -1 → transitions to FALL in advanceAnim()
};
static const FrameData DENNIS_FALL_SEQ[] = {
    {30,3,1}, {31,3,2}, {32,3,3}, {33,3,4}, {34,3,-1}
};
static const FrameData DENNIS_DEAD_SEQ[] = {
    {44,99,0}                  // loops on self
};

// ── Sequence table (indexed by St enum) ───────────────────────────────────────
// Order MUST match enum St: IDLE WALK JUMP ATTACK FALL DEAD ATTACK2 HIT HIT_KD RUN DASH
static const FrameData* DENNIS_SEQS[] = {
    DENNIS_IDLE_SEQ,     // St::IDLE    = 0
    DENNIS_WALK_SEQ,     // St::WALK    = 1
    DENNIS_JUMP_SEQ,     // St::JUMP    = 2
    DENNIS_ATTACK_SEQ,   // St::ATTACK  = 3
    DENNIS_FALL_SEQ,     // St::FALL    = 4
    DENNIS_DEAD_SEQ,     // St::DEAD    = 5
    DENNIS_ATTACK2_SEQ,  // St::ATTACK2 = 6
    DENNIS_HIT_SEQ,      // St::HIT     = 7
    DENNIS_HIT_KD_SEQ,   // St::HIT_KD  = 8
    DENNIS_RUN_SEQ,      // St::RUN     = 9
    DENNIS_DASH_SEQ,     // St::DASH    = 10
};
