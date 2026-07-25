#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "types.hpp"

// ── Texture loader ────────────────────────────────────────────────────────────
// colorKey=true  → magenta (255,0,128) is transparent (character sprites)
// colorKey=false → opaque  (background tiles)
// Transparent color defaults to LF2's magenta (255,0,128), but some sheets were
// exported with a different key — e.g. firen_0.png uses pure black — so the key
// is overridable per texture.
inline SDL_Texture* loadTex(SDL_Renderer* r, const char* path, bool colorKey = true,
                            Uint8 kr = 255, Uint8 kg = 0, Uint8 kb = 128) {
    SDL_Surface* s = IMG_Load(path);
    if (!s) return nullptr;
    if (colorKey) SDL_SetColorKey(s, SDL_TRUE, SDL_MapRGB(s->format, kr, kg, kb));
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

// ── Background layers are drawn data-driven in main.cpp (renderBackground),
//    straight from bg.dat via dat::Background. The old fixed drawTiled/drawOnce
//    helpers were removed with the hardcoded Lion Forest renderer.

// ── Character sprite (from sheet grid) ───────────────────────────────────────
inline void drawSprite(SDL_Renderer* r, SDL_Texture* t, int pic,
                       int sx, int sy, bool flipH,
                       Uint8 cr = 255, Uint8 cg = 255, Uint8 cb = 255)
{
    if (!t) return;
    SDL_SetTextureColorMod(t, cr, cg, cb);
    SDL_Rect src = { pic % SHEET_COLS * FRAME_W, pic / SHEET_COLS * FRAME_H, FRAME_W, FRAME_H };
    SDL_Rect dst = { sx, sy, SW, SH };
    SDL_RenderCopyEx(r, t, &src, &dst, 0, nullptr,
                     flipH ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

// ── HUD portrait (scaled to arbitrary dw×dh) ─────────────────────────────────
inline void drawSpriteAt(SDL_Renderer* r, SDL_Texture* t, int pic,
                         int dx, int dy, int dw, int dh, bool flipH)
{
    if (!t) return;
    SDL_Rect src = { pic % SHEET_COLS * FRAME_W, pic / SHEET_COLS * FRAME_H, FRAME_W, FRAME_H };
    SDL_Rect dst = { dx, dy, dw, dh };
    SDL_RenderCopyEx(r, t, &src, &dst, 0, nullptr,
                     flipH ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

// ── HP bar ────────────────────────────────────────────────────────────────────
inline void drawHpBar(SDL_Renderer* r, int bx, int by,
                      int hp, int maxHp,
                      Uint8 cr, Uint8 cg, Uint8 cb)
{
    constexpr int BW = 200, BH = 14;
    SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    SDL_Rect bg = { bx, by, BW, BH };
    SDL_RenderFillRect(r, &bg);
    int fill = (maxHp > 0 && hp > 0) ? BW * hp / maxHp : 0;
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect bar = { bx, by, fill, BH };
    SDL_RenderFillRect(r, &bar);
}

// ── Filled rectangle (with optional alpha) ────────────────────────────────────
inline void fillRect(SDL_Renderer* r, int x, int y, int w, int h,
                     Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca = 255)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}
