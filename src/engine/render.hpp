#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "types.hpp"

// ── Texture loader ────────────────────────────────────────────────────────────
// colorKey=true  → magenta (255,0,128) is transparent (character sprites)
// colorKey=false → opaque  (background tiles)
inline SDL_Texture* loadTex(SDL_Renderer* r, const char* path, bool colorKey = true) {
    SDL_Surface* s = IMG_Load(path);
    if (!s) return nullptr;
    if (colorKey) SDL_SetColorKey(s, SDL_TRUE, SDL_MapRGB(s->format, 255, 0, 128));
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

// ── Background helpers ────────────────────────────────────────────────────────
inline void drawTiled(SDL_Renderer* r, SDL_Texture* t, int y, int tw, int th, int camX) {
    if (!t) return;
    for (int x = 0; x < MAP_W; x += tw) {
        SDL_Rect d = { x - camX, y, tw, th };
        if (d.x + tw < 0 || d.x > SCREEN_W) continue;
        SDL_RenderCopy(r, t, nullptr, &d);
    }
}

inline void drawOnce(SDL_Renderer* r, SDL_Texture* t,
                     int mx, int y, int tw, int th, int camX)
{
    if (!t) return;
    SDL_Rect d = { mx - camX, y, tw, th };
    if (d.x + tw < 0 || d.x > SCREEN_W) return;
    SDL_RenderCopy(r, t, nullptr, &d);
}

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
