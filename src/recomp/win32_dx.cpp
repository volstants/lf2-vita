// win32_dx.cpp — GDI bitmaps, DirectDraw, DirectSound and DirectShow for the
// recompiled lf2.exe. Mirrors tools/emu/runtime.py; see win32_api.cpp.
#include "win32_internal.hpp"
#include "gdi_font.h"

#include <algorithm>
#include <cstdio>

namespace win32 {

const uint32_t SCREEN_W = 794, SCREEN_H = 550;
bool g_noBlit = false;   // profiling: skip pixel work
bool g_noAds = false, g_hideCursor = false;
bool g_hudDrawn = false;

static bool isAdSheet(std::string n) {
    for (char& c : n) { c = (char)tolower((unsigned char)c); if (c == '/') c = '\\'; }
    return n == "sprite\\sys\\ad0.bmp" || n == "sprite\\sys\\ad1.bmp";
}
uint64_t g_blitPixels = 0, g_blitCalls = 0;
static const uint32_t E_FAIL_ = 0x80004005u;
extern std::string g_lastWave;

// ── BMP decoding ────────────────────────────────────────────────────────────
static uint32_t rd32le(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int32_t rds32le(const uint8_t* p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16le(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }

std::shared_ptr<Bitmap> decodeDib(const uint8_t* d, size_t n, int offBits) {
    if (n < 40) return nullptr;
    uint32_t hsz = rd32le(d);
    int32_t w = rds32le(d + 4), h = rds32le(d + 8);
    uint16_t bpp = rd16le(d + 14);
    uint32_t comp = rd32le(d + 16);
    uint32_t clrUsed = hsz >= 36 ? rd32le(d + 32) : 0;
    bool topdown = h < 0;
    if (h < 0) h = -h;
    int ncol = clrUsed ? (int)clrUsed : (bpp <= 8 ? 1 << bpp : 0);
    auto b = std::make_shared<Bitmap>();
    b->w = w; b->h = h;
    for (int i = 0; i < ncol && i < 256; ++i) {
        const uint8_t* q = d + hsz + 4 * i;
        b->pal[i] = ((uint32_t)q[2] << 16) | ((uint32_t)q[1] << 8) | q[0];
    }
    if (offBits < 0) offBits = (int)(hsz + 4 * ncol);
    const uint8_t* px = d + offBits;
    size_t pn = n > (size_t)offBits ? n - offBits : 0;
    auto row = [&](int y) { return topdown ? y : h - 1 - y; };   // file row -> image row
    if (bpp == 8 || bpp == 4) {
        b->indexed = true;
        b->idx.assign((size_t)w * h, 0);
        if (bpp == 8 && comp == 1) {                       // RLE8
            int x = 0, y = 0;
            size_t i = 0;
            uint8_t* rowp = h > 0 ? &b->idx[(size_t)row(0) * w] : nullptr;
            while (i + 1 < pn && y < h) {
                uint8_t c = px[i], v = px[i + 1];
                i += 2;
                if (c > 0) {
                    if (x < w) memset(rowp + x, v, (size_t)std::min<int>(c, w - x));
                    x += c;
                } else if (v == 0) { x = 0; ++y; if (y < h) rowp = &b->idx[(size_t)row(y) * w]; }
                else if (v == 1) break;
                else if (v == 2) {
                    if (i + 1 < pn) { x += px[i]; y += px[i + 1]; if (y < h) rowp = &b->idx[(size_t)row(y) * w]; }
                    i += 2;
                } else {
                    int k = std::min<int>(v, (int)(pn - i));
                    if (x < w) memcpy(rowp + x, px + i, (size_t)std::min(k, w - x));
                    x += v;
                    i += v + (v & 1);
                }
            }
        } else if (bpp == 8) {
            size_t stride = ((size_t)w + 3) & ~(size_t)3;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    size_t o = (size_t)y * stride + x;
                    b->idx[(size_t)row(y) * w + x] = o < pn ? px[o] : 0;
                }
        } else {
            size_t stride = (((size_t)w + 1) / 2 + 3) & ~(size_t)3;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    size_t o = (size_t)y * stride + x / 2;
                    uint8_t v = o < pn ? px[o] : 0;
                    b->idx[(size_t)row(y) * w + x] = (x & 1) ? (v & 15) : (v >> 4);
                }
        }
    } else if (bpp == 24) {
        b->rgb.assign((size_t)w * h, 0);
        size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t o = (size_t)y * stride + 3 * x;
                if (o + 2 < pn)
                    b->rgb[(size_t)row(y) * w + x] = ((uint32_t)px[o + 2] << 16) | ((uint32_t)px[o + 1] << 8) | px[o];
            }
    } else {
        return nullptr;
    }
    return b;
}

std::shared_ptr<Bitmap> decodeBmpFile(const std::vector<uint8_t>& f) {
    if (f.size() < 54 || f[0] != 'B' || f[1] != 'M') return nullptr;
    uint32_t off = rd32le(&f[10]);
    return decodeDib(f.data() + 14, f.size() - 14, (int)off - 14);
}

// ── resources (RT_BITMAP by name) ───────────────────────────────────────────
static std::map<std::string, std::pair<uint32_t, uint32_t>> g_res;   // name -> (addr,size)
static void indexResources() {
    g_res.clear();
    uint32_t pe = RD32(0x400000 + 0x3c);
    uint32_t opt = 0x400000 + pe + 24;
    uint32_t rsrcRva = RD32(opt + 112);
    uint32_t base = 0x400000 + rsrcRva;
    auto entries = [](uint32_t dir, auto&& fn) {
        uint16_t named = (uint16_t)RD16(dir + 12), ids = (uint16_t)RD16(dir + 14);
        for (uint32_t i = 0; i < (uint32_t)named + ids; ++i) fn(RD32(dir + 16 + 8 * i), RD32(dir + 20 + 8 * i));
    };
    entries(base, [&](uint32_t typeName, uint32_t typeOff) {
        if (typeName != 2 || !(typeOff & 0x80000000u)) return;
        entries(base + (typeOff & 0x7fffffff), [&](uint32_t name, uint32_t off) {
            if (!(name & 0x80000000u) || !(off & 0x80000000u)) return;
            uint32_t s = base + (name & 0x7fffffff);
            uint16_t len = (uint16_t)RD16(s);
            std::string nm;
            for (uint16_t k = 0; k < len; ++k) nm.push_back((char)toupper(RD16(s + 2 + 2 * k)));
            uint32_t langDir = base + (off & 0x7fffffff);
            uint32_t dataEnt = base + RD32(langDir + 20);
            g_res[nm] = {0x400000 + RD32(dataEnt), RD32(dataEnt + 4)};
        });
    });
}

// ── GDI ─────────────────────────────────────────────────────────────────────
static std::map<uint32_t, std::shared_ptr<Bitmap>> g_bitmaps;
struct Dc { int kind = 0; uint32_t obj = 0; uint32_t text = 0, bk = 0xFFFFFF; };  // 0 none, 1 bitmap, 2 surface
static std::map<uint32_t, Dc> g_dcs;
static std::map<std::string, std::weak_ptr<Bitmap>> g_loadedByName;

static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

// "X_mirror.bmp" whose pixels are exactly X.bmp flipped: share the pixels.
static std::shared_ptr<Bitmap> dedupMirror(const std::string& name, std::shared_ptr<Bitmap> b) {
    std::string n = lower(name);
    size_t p = n.rfind("_mirror.bmp");
    if (p == std::string::npos || !b->indexed) return b;
    auto it = g_loadedByName.find(n.substr(0, p) + ".bmp");
    if (it == g_loadedByName.end()) return b;
    auto base = it->second.lock();
    if (!base || !base->indexed || base->w != b->w || base->h != b->h ||
        memcmp(base->pal, b->pal, sizeof b->pal) != 0) return b;
    const int w = b->w;
    for (int y = 0; y < b->h; ++y) {
        const uint8_t* s = &base->idx[(size_t)y * w];
        const uint8_t* m = &b->idx[(size_t)y * w];
        int x = 0;
        for (; x + 8 <= w; x += 8) {           // 8 pixels at a time: m[x..x+7] == reverse(s[w-8-x..w-1-x])
            uint64_t a, c;
            memcpy(&a, m + x, 8);
            memcpy(&c, s + w - 8 - x, 8);
            if (a != __builtin_bswap64(c)) return b;
        }
        for (; x < w; ++x) if (m[x] != s[w - 1 - x]) return b;
    }
    auto view = std::make_shared<Bitmap>();
    view->w = b->w; view->h = b->h; view->indexed = true;
    memcpy(view->pal, b->pal, sizeof view->pal);
    view->flipOf = base;
    return view;
}

W32_STD(g_LoadImageA, 6) {
    uint32_t namep = A(1), flags = A(5);
    std::string name = namep > 0xFFFF ? cstr(namep) : std::string();
    std::shared_ptr<Bitmap> bmp;
    if (flags & 0x10) {
        std::vector<uint8_t> f;
        if (name.empty() || (g_noAds && isAdSheet(name)) || !host::readFile(name, f)) return 0;
        bmp = decodeBmpFile(f);
        if (!bmp) return 0;
        bmp = dedupMirror(name, bmp);
        g_loadedByName[lower(name)] = bmp;
    } else {
        std::string up = name;
        for (char& c : up) c = (char)toupper((unsigned char)c);
        auto it = g_res.find(up);
        if (it == g_res.end()) return 0;
        bmp = decodeDib(M + it->second.first, it->second.second, -1);
        if (!bmp) return 0;
        if (g_hideCursor && up.compare(0, 10, "LF2_CURSOR") == 0) bmp->hidden = true;
        if (up == "FRAME") bmp->hudFrame = true;
        // MENU_CLIP7 column 648-710, rows 40-100: the title's "Update on/off"
        // toggle for downloading ads (top-right corner)
        if (g_noAds && up == "MENU_CLIP7") {
            int r[4] = {648, 40, 710, 100};
            memcpy(bmp->hideRect, r, sizeof r);
        }
    }
    uint32_t h = newHandle();
    g_bitmaps[h] = bmp;
    return h;
}
W32_STD(g_GetObjectA, 3) {
    auto it = g_bitmaps.find(A(0));
    if (it == g_bitmaps.end()) return 0;
    int32_t w = it->second->w, h = it->second->h;
    uint8_t b[24];
    int32_t v[4] = {0, w, h, (w * 3 + 3) & ~3};
    memcpy(b, v, 16);
    uint16_t pl = 1, bp = 24;
    memcpy(b + 16, &pl, 2); memcpy(b + 18, &bp, 2);
    uint32_t z = 0; memcpy(b + 20, &z, 4);
    memcpy(M + A(2), b, 24);
    return 24;
}
W32_STD(g_CreateCompatibleDC, 1) { uint32_t h = newHandle(); g_dcs[h] = Dc{}; return h; }
W32_STD(g_SelectObject, 2) {
    if (g_bitmaps.count(A(1))) g_dcs[A(0)] = Dc{1, A(1)};
    return 1;
}
W32_STD(g_DeleteDC, 1) { g_dcs.erase(A(0)); return 1; }
W32_STD(g_DeleteObject, 1) { g_bitmaps.erase(A(0)); return 1; }
static uint32_t colorref(uint32_t c) {        // 0x00BBGGRR -> XRGB
    return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
}
W32_STD(g_SetBkColor, 2) { auto it = g_dcs.find(A(0)); if (it != g_dcs.end()) it->second.bk = A(1); return 0; }
W32_STD(g_SetTextColor, 2) { auto it = g_dcs.find(A(0)); if (it != g_dcs.end()) it->second.text = A(1); return 0; }
// GDI TextOut with the DC's default font and OPAQUE background, using the
// bitmap stand-in for the "System" font (gdi_font.h; same glyphs in the oracle).
W32_STD(g_TextOutA, 5) {
    auto it = g_dcs.find(A(0));
    if (it == g_dcs.end() || it->second.kind != 2) return 1;
    Surface* s = surfaceOf(it->second.obj);
    int x = Ai(1), y = Ai(2), n = Ai(4);
    uint32_t fg = colorref(it->second.text), bg = colorref(it->second.bk);
    int width = 0;
    for (int i = 0; i < n; ++i) width += gdifont::W[M[A(3) + i]];
    s->makeRgb();
    for (int yy = 0; yy < gdifont::H; ++yy) {
        int py = y + yy;
        if (py < 0 || py >= s->h) continue;
        int px = x;
        for (int i = 0; i < n; ++i) {
            uint8_t c = M[A(3) + i];
            uint16_t bits = gdifont::ROWS[c * gdifont::H + yy];
            for (int b = 0; b < gdifont::W[c]; ++b, ++px)
                if (px >= 0 && px < s->w) s->rgb[(size_t)py * s->w + px] = (bits >> b & 1) ? fg : bg;
        }
    }
    (void)width;
    return 1;
}

void Surface::makeRgb() {
    if (!rgb.empty()) return;
    rgb.assign((size_t)w * h, 0);
    if (src)
        for (int y = 0; y < h && y < src->h; ++y)
            for (int x = 0; x < w && x < src->w; ++x) rgb[(size_t)y * w + x] = src->at(x, y);
    src.reset();
}

// Paste `sw x sh` source pixels (already scaled/mirrored via fetch) at (x,y),
// clipped to the destination: runtime.py _paste.
template <typename Fetch>
static void paste(Surface& dst, int x, int y, int w, int h, Fetch fetch) {
    int x0 = std::max(x, 0), y0 = std::max(y, 0);
    int x1 = std::min(x + w, dst.w), y1 = std::min(y + h, dst.h);
    if (x1 <= x0 || y1 <= y0) return;
    dst.makeRgb();
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* d = &dst.rgb[(size_t)yy * dst.w];
        for (int xx = x0; xx < x1; ++xx) {
            uint32_t v;
            if (fetch(xx - x, yy - y, v)) d[xx] = v;
        }
    }
}

W32_STD(g_StretchBlt, 11) {
    uint32_t dd = A(0), sd = A(5);
    int dx = Ai(1), dy = Ai(2), dw = Ai(3), dh = Ai(4);
    int sx = Ai(6), sy = Ai(7), sw = Ai(8), sh = Ai(9);
    auto di = g_dcs.find(dd), si = g_dcs.find(sd);
    if (di == g_dcs.end() || si == g_dcs.end() || di->second.kind != 2 || si->second.kind != 1) return 1;
    Surface* s = surfaceOf(di->second.obj);
    auto bmp = g_bitmaps[si->second.obj];
    if (bmp->hidden) s->hidden = true;
    if (bmp->hideRect[2]) s->hideRect = bmp->hideRect;
    if (bmp->hudFrame) s->hudFrame = true;
    // numpy slicing img[sy:sy+sh, sx:sx+sw] clamps to the bitmap
    int rx0 = sx, ry0 = sy;
    int rw = std::max(0, std::min(sx + sw, bmp->w) - sx), rh = std::max(0, std::min(sy + sh, bmp->h) - sy);
    bool scale = (sw != dw || sh != dh) && sw > 0 && sh > 0 && dw > 0 && dh > 0;
    // whole-surface copy of an indexed bitmap: adopt it instead of expanding
    if (!scale && dx == 0 && dy == 0 && sx == 0 && sy == 0 && bmp->indexed &&
        rw == s->w && rh == s->h && bmp->w == s->w && bmp->h == s->h && s->rgb.empty()) {
        s->src = bmp;
        return 1;
    }
    if (scale) {
        int outw = dw, outh = dh;
        paste(*s, dx, dy, outw, outh, [&](int x, int y, uint32_t& v) {
            int srcy = y * sh / dh, srcx = x * sw / dw;
            if (srcy >= rh || srcx >= rw) return false;
            v = bmp->at(rx0 + srcx, ry0 + srcy);
            return true;
        });
    } else {
        paste(*s, dx, dy, rw, rh, [&](int x, int y, uint32_t& v) { v = bmp->at(rx0 + x, ry0 + y); return true; });
    }
    return 1;
}

// ── COM plumbing ────────────────────────────────────────────────────────────
struct ComKind { std::vector<ComMethod> methods; uint32_t vtable = 0; };
static std::map<std::string, ComKind> g_kinds;
static std::unordered_map<uint32_t, void*> g_comNative;

void registerComKind(const char* kind, std::vector<ComMethod> methods) {
    g_kinds[kind] = ComKind{std::move(methods), 0};
}
static void comDefault() { /* patched per method below */ }

uint32_t comObject(const char* kind, void* native) {
    ComKind& k = g_kinds.at(kind);
    if (!k.vtable) {
        k.vtable = rt::alloc(4 * (uint32_t)k.methods.size());
        for (size_t i = 0; i < k.methods.size(); ++i) {
            uint32_t addr = rt::newStub(k.methods[i].name, k.methods[i].fn);
            WR32(k.vtable + 4 * (uint32_t)i, addr);
        }
    }
    uint32_t obj = rt::alloc(0x40);
    WR32(obj, k.vtable);
    g_comNative[obj] = native;
    return obj;
}
void* comNative(uint32_t obj) {
    auto it = g_comNative.find(obj);
    return it == g_comNative.end() ? nullptr : it->second;
}

// Default COM method: returns 0 and pops `nargs` (generated per arity).
template <int N> static void comZero() { ret(N, 0); }
static rt::StubFn zeroFor(int n) {
    static const rt::StubFn t[] = {comZero<0>, comZero<1>, comZero<2>, comZero<3>, comZero<4>,
                                   comZero<5>, comZero<6>, comZero<7>, comZero<8>, comZero<9>,
                                   comZero<10>, comZero<11>, comZero<12>};
    return t[n];
}

// ── DirectDraw ──────────────────────────────────────────────────────────────
static std::map<uint32_t, std::unique_ptr<Surface>> g_surfaces;
static Surface* g_primary = nullptr;
Surface* surfaceOf(uint32_t obj) {
    auto it = g_surfaces.find(obj);
    if (it == g_surfaces.end()) rt::fatal("not a surface %08x", obj);
    return it->second.get();
}
const Surface* primarySurface() { return g_primary; }
void surfaceToXRGB(const Surface* s, std::vector<uint32_t>& out, int& w, int& h) {
    w = s->w; h = s->h;
    out.resize((size_t)w * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) out[(size_t)y * w + x] = s->px(x, y);
}

static void fillPf(uint32_t p) {
    uint32_t v[8] = {0x20, 0x40, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0};
    memcpy(M + p, v, 32);
}
static void fillDesc(uint32_t p, int w, int h, uint32_t caps, uint32_t mem) {
    memset(M + p, 0, 0x6C);
    uint32_t v[5] = {0x6C, 0x100F, (uint32_t)h, (uint32_t)w, (uint32_t)(w * 4)};
    memcpy(M + p, v, 20);
    WR32(p + 0x24, mem);
    fillPf(p + 0x48);
    WR32(p + 0x68, caps);
}

W32_STD(dd_CreateClipper, 4) { WR32(A(2), comObject("clip", nullptr)); return 0; }
W32_STD(dd_CreateSurface, 4) {
    uint32_t desc = A(1);
    uint32_t flags = RD32(desc + 4);
    int h = (flags & 2) ? (int)RD32(desc + 8) : (int)SCREEN_H;
    int w = (flags & 4) ? (int)RD32(desc + 12) : (int)SCREEN_W;
    auto s = std::make_unique<Surface>();
    s->w = w; s->h = h; s->caps = RD32(desc + 0x68);
    Surface* raw = s.get();
    uint32_t obj = comObject("surf", raw);
    g_surfaces[obj] = std::move(s);
    if (raw->caps & 0x200) g_primary = raw;
    WR32(A(2), obj);
    return 0;
}
W32_STD(dd_GetDisplayMode, 2) { fillDesc(A(1), SCREEN_W, SCREEN_H, 0, 0); return 0; }

W32_STD(s_GetPixelFormat, 2) { fillPf(A(1)); return 0; }
W32_STD(s_GetSurfaceDesc, 2) { Surface* s = surfaceOf(A(0)); fillDesc(A(1), s->w, s->h, s->caps, 0); return 0; }
W32_STD(s_SetColorKey, 3) {
    Surface* s = surfaceOf(A(0));
    if (A(2)) { s->hasKey = true; s->keyLo = RD32(A(2)); s->keyHi = RD32(A(2) + 4); }
    else s->hasKey = false;
    return 0;
}
W32_STD(s_GetDC, 2) { uint32_t h = newHandle(); g_dcs[h] = Dc{2, A(0)}; WR32(A(1), h); return 0; }
W32_STD(s_ReleaseDC, 2) { g_dcs.erase(A(1)); return 0; }
W32_STD(s_Lock, 5) {
    Surface* s = surfaceOf(A(0));
    s->makeRgb();
    if (!s->lockMem) s->lockMem = rt::alloc((uint32_t)(s->w * s->h * 4));
    memcpy(M + s->lockMem, s->rgb.data(), (size_t)s->w * s->h * 4);
    fillDesc(A(2), s->w, s->h, s->caps, s->lockMem);
    return 0;
}
W32_STD(s_Unlock, 2) {
    Surface* s = surfaceOf(A(0));
    if (s->lockMem) { s->makeRgb(); memcpy(s->rgb.data(), M + s->lockMem, (size_t)s->w * s->h * 4); }
    return 0;
}
static void presentPrimary() {
    if (!g_primary || g_noBlit) return;
    g_primary->makeRgb();
    host::present(g_primary->rgb.data(), g_primary->w, g_primary->h);
}
W32_STD(s_Flip, 3) { presentPrimary(); return 0; }

static void readRect(uint32_t p, const Surface& s, int r[4]) {
    if (!p) { r[0] = 0; r[1] = 0; r[2] = s.w; r[3] = s.h; return; }
    for (int i = 0; i < 4; ++i) r[i] = (int32_t)RD32(p + 4 * i);
}

static void blit(Surface& dst, const int d[4], const Surface& src, const int sr[4],
                 bool key, bool mirror, bool flipv) {
    if (src.hudFrame) g_hudDrawn = true;
    if (g_noBlit || src.hidden) return;
    if (src.hideRect && sr[0] >= src.hideRect[0] && sr[2] <= src.hideRect[2] &&
        sr[1] >= src.hideRect[1] && sr[3] <= src.hideRect[3]) return;
    int dl = d[0], dt = d[1], dw = d[2] - d[0], dh = d[3] - d[1];
    int sl = sr[0], st = sr[1], sw = sr[2] - sr[0], sh = sr[3] - sr[1];
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    sl = std::max(sl, 0); st = std::max(st, 0);
    if (sl + sw > src.w || st + sh > src.h) return;     // numpy shape mismatch -> no blit
    bool scale = sw != dw || sh != dh;
    uint32_t lo = src.keyLo, hi = src.keyHi;
    const Bitmap* ib = (src.rgb.empty() && src.src && src.src->indexed) ? src.src.get() : nullptr;
    bool ibFlip = false;
    if (ib && ib->flipOf) { ib = ib->flipOf.get(); ibFlip = true; }
    bool trans[256];
    if (ib && key) for (int i = 0; i < 256; ++i) trans[i] = ib->pal[i] >= lo && ib->pal[i] <= hi;
    int x0 = std::max(dl, 0), y0 = std::max(dt, 0);
    int x1 = std::min(dl + dw, dst.w), y1 = std::min(dt + dh, dst.h);
    if (x1 <= x0 || y1 <= y0) return;
    dst.makeRgb();
    g_blitPixels += (uint64_t)(x1 - x0) * (y1 - y0);
    ++g_blitCalls;
    const int n = x1 - x0;
    if (!scale && !flipv) {
        // Fast path: 1:1 rows, optional horizontal mirror and colour key.
        // Source column of destination x: sl + (mirror ? dw-1-(x-dl) : x-dl).
        for (int yy = y0; yy < y1; ++yy) {
            int sy = st + (yy - dt);
            uint32_t* out = &dst.rgb[(size_t)yy * dst.w + x0];
            int sx0 = mirror ? sl + dw - 1 - (x0 - dl) : sl + (x0 - dl);
            int step = mirror ? -1 : 1;
            if (ib) {
                const uint8_t* irow = &ib->idx[(size_t)sy * ib->w];
                const uint32_t* pal = ib->pal;
                int sx = ibFlip ? ib->w - 1 - sx0 : sx0;
                int istep = ibFlip ? -step : step;
                const uint8_t* p = irow + sx;
                if (key) {
                    for (int i = 0; i < n; ++i, p += istep) { uint8_t c = *p; if (!trans[c]) out[i] = pal[c]; }
                } else {
                    for (int i = 0; i < n; ++i, p += istep) out[i] = pal[*p];
                }
            } else if (!src.rgb.empty()) {
                const uint32_t* p = &src.rgb[(size_t)sy * src.w + sx0];
                if (!key && !mirror) memcpy(out, p, (size_t)n * 4);
                else if (key) { for (int i = 0; i < n; ++i, p += step) { uint32_t v = *p; if (v < lo || v > hi) out[i] = v; } }
                else for (int i = 0; i < n; ++i, p += step) out[i] = *p;
            } else if (src.src) {
                for (int i = 0; i < n; ++i) {
                    uint32_t v = src.src->at(sx0 + i * step, sy);
                    if (!key || v < lo || v > hi) out[i] = v;
                }
            } else if (!key || (0 < lo || 0 > hi)) {
                for (int i = 0; i < n; ++i) out[i] = 0;
            }
        }
        return;
    }
    for (int yy = y0; yy < y1; ++yy) {
        int ry = yy - dt;                         // row in the (scaled) region
        if (flipv) ry = dh - 1 - ry;
        int sy = st + (scale ? ry * sh / dh : ry);
        uint32_t* out = &dst.rgb[(size_t)yy * dst.w];
        for (int xx = x0; xx < x1; ++xx) {
            int rx = xx - dl;
            if (mirror) rx = dw - 1 - rx;
            int sx = sl + (scale ? rx * sw / dw : rx);
            uint32_t v;
            if (ib) {
                uint8_t i = ib->idx[(size_t)sy * ib->w + (ibFlip ? ib->w - 1 - sx : sx)];
                if (key && trans[i]) continue;
                v = ib->pal[i];
            } else {
                v = src.px(sx, sy);
                if (key && v >= lo && v <= hi) continue;
            }
            out[xx] = v;
        }
    }
}

W32_STD(s_Blt, 6) {
    Surface* dst = surfaceOf(A(0));
    uint32_t flags = A(4), fx = A(5);
    int d[4];
    readRect(A(1), *dst, d);
    if (flags & 0x400) {
        uint32_t color = RD32(fx + 0x50) & 0xFFFFFF;
        int l = std::max(d[0], 0), t = std::max(d[1], 0), r = std::min(d[2], dst->w), b = std::min(d[3], dst->h);
        if (r > l && b > t) {
            dst->makeRgb();
            for (int y = t; y < b; ++y) std::fill(&dst->rgb[(size_t)y * dst->w + l], &dst->rgb[(size_t)y * dst->w + r], color);
        }
        return 0;
    }
    if (!A(2)) return 0;
    Surface* src = surfaceOf(A(2));
    int s[4];
    readRect(A(3), *src, s);
    bool ddfx = (flags & 0x800) && fx;
    bool mirror = ddfx && (RD32(fx + 4) & 2);
    bool flipv = ddfx && (RD32(fx + 4) & 4);
    bool key = (flags & 0x8000) && src->hasKey;
    blit(*dst, d, *src, s, key, mirror, flipv);
    if (dst == g_primary) presentPrimary();
    return 0;
}
W32_STD(s_BltFast, 6) {
    Surface* dst = surfaceOf(A(0));
    int x = Ai(1), y = Ai(2);
    Surface* src = surfaceOf(A(3));
    int s[4];
    readRect(A(4), *src, s);
    int d[4] = {x, y, x + s[2] - s[0], y + s[3] - s[1]};
    blit(*dst, d, *src, s, (A(5) & 1) && src->hasKey, false, false);
    return 0;
}

// ── DirectSound ─────────────────────────────────────────────────────────────
struct SoundBuf {
    int id = 0;
    std::string name;
    uint32_t size = 0, mem = 0;
    int channels = 1, rate = 44100, bits = 16;
    int vol = 0, pan = 0;
};
static std::vector<std::unique_ptr<SoundBuf>> g_sbufs;
static SoundBuf* sb(uint32_t obj) { return (SoundBuf*)comNative(obj); }

W32_STD(ds_CreateSoundBuffer, 4) {
    uint32_t desc = A(1);
    auto b = std::make_unique<SoundBuf>();
    b->id = (int)g_sbufs.size();
    b->name = g_lastWave;
    b->size = RD32(desc + 8);
    uint32_t fmt = RD32(desc + 16);
    if (fmt) { b->channels = (int)RD16(fmt + 2); b->rate = (int)RD32(fmt + 4); b->bits = (int)RD16(fmt + 14); }
    uint32_t obj = comObject("dsb", b.get());
    g_sbufs.push_back(std::move(b));
    WR32(A(2), obj);
    return 0;
}
W32_STD(ds_DuplicateSoundBuffer, 3) {
    SoundBuf* o = sb(A(1));
    auto b = std::make_unique<SoundBuf>(*o);
    b->id = (int)g_sbufs.size();
    b->vol = 0; b->pan = 0;
    uint32_t obj = comObject("dsb", b.get());
    g_sbufs.push_back(std::move(b));
    WR32(A(2), obj);
    return 0;
}
W32_STD(dsb_Lock, 8) {
    SoundBuf* b = sb(A(0));
    if (!b->mem) b->mem = rt::alloc(std::max<uint32_t>(b->size, 16));
    WR32(A(3), b->mem);
    WR32(A(4), A(2) ? A(2) : b->size);
    if (A(5)) WR32(A(5), 0);
    if (A(6)) WR32(A(6), 0);
    return 0;
}
W32_STD(dsb_Play, 4) {
    SoundBuf* b = sb(A(0));
    if (b->mem)
        host::soundPlay(b->id, M + b->mem, b->size, b->channels, b->rate, b->bits, b->vol, b->pan);
    return 0;
}
// Same result as the default method (returns 0, pops 5); it also lets the host
// convert the sound now, while the game is loading, instead of at its first
// play in a fight.
W32_STD(dsb_Unlock, 5) {
    SoundBuf* b = sb(A(0));
    if (b->mem) host::soundPrepare(M + b->mem, b->size, b->channels, b->rate, b->bits);
    return 0;
}
W32_STD(dsb_Stop, 1) { host::soundStop(sb(A(0))->id); return 0; }
W32_STD(dsb_SetVolume, 2) { sb(A(0))->vol = Ai(1); return 0; }
W32_STD(dsb_SetPan, 2) { sb(A(0))->pan = Ai(1); return 0; }
W32_STD(dsb_GetStatus, 2) { WR32(A(1), 0); return 0; }

// ── DirectShow (music) ──────────────────────────────────────────────────────
static std::string g_musicFile;
static const char* iidKind(uint32_t p) {
    switch (RD32(p)) {
    case 0x56a868a9: return "graph";
    case 0x56a868b1: return "mctrl";
    case 0x56a868b6: return "mevent";
    case 0x56a868b2: return "mpos";
    case 0x56a868b3: return "baudio";
    }
    return nullptr;
}
W32_STD(x_CoCreateInstance, 5) {
    const char* k = iidKind(A(3));
    if (!k || strcmp(k, "graph") != 0) return E_FAIL_;
    WR32(A(4), comObject("graph", nullptr));
    return 0;
}
W32_STD(dshow_QueryInterface, 3) {
    const char* k = iidKind(A(1));
    if (!k) { WR32(A(2), 0); return 0x80004002u; }
    WR32(A(2), comObject(k, nullptr));
    return 0;
}
W32_STD(graph_RenderFile, 3) {
    std::string s;
    for (uint32_t p = A(1); RD16(p); p += 2) s.push_back((char)RD16(p));
    g_musicFile = s;
    return 0;
}
W32_STD(mctrl_Run, 1) { host::musicPlay(g_musicFile); return 0; }
W32_STD(mctrl_Stop, 1) { host::musicStop(); return 0; }
W32_STD(mevent_GetEvent, 5) { return 0x80004004u; }
W32_STD(baudio_put_Volume, 2) { host::musicVolume(Ai(1)); return 0; }

W32_STD(x_DirectDrawCreate, 3) { WR32(A(1), comObject("dd", nullptr)); return 0; }
W32_STD(x_DirectSoundCreate, 3) { WR32(A(1), comObject("ds", nullptr)); return 0; }

struct Spec { const char* name; int nargs; rt::StubFn fn; };
static std::vector<ComMethod> build(std::initializer_list<Spec> l) {
    std::vector<ComMethod> v;
    for (const Spec& s : l) v.push_back(ComMethod{s.name, s.nargs, s.fn ? s.fn : zeroFor(s.nargs)});
    return v;
}
#define DISPATCH_METHODS {"GetTypeInfoCount", 2, nullptr}, {"GetTypeInfo", 4, nullptr}, \
    {"GetIDsOfNames", 6, nullptr}, {"Invoke", 9, nullptr}

void initDirectX() {
    indexResources();
    g_surfaces.clear(); g_primary = nullptr; g_sbufs.clear(); g_bitmaps.clear(); g_dcs.clear();
    registerComKind("dd", build({{"QueryInterface", 3}, {"AddRef", 1}, {"Release", 1}, {"Compact", 1},
        {"CreateClipper", 4, dd_CreateClipper}, {"CreatePalette", 5}, {"CreateSurface", 4, dd_CreateSurface},
        {"DuplicateSurface", 3}, {"EnumDisplayModes", 5}, {"EnumSurfaces", 5}, {"FlipToGDISurface", 1},
        {"GetCaps", 3}, {"GetDisplayMode", 2, dd_GetDisplayMode}, {"GetFourCCCodes", 3},
        {"GetGDISurface", 2}, {"GetMonitorFrequency", 2}, {"GetScanLine", 2},
        {"GetVerticalBlankStatus", 2}, {"Initialize", 2}, {"RestoreDisplayMode", 1},
        {"SetCooperativeLevel", 3}, {"SetDisplayMode", 4}, {"WaitForVerticalBlank", 3}}));
    registerComKind("surf", build({{"QueryInterface", 3}, {"AddRef", 1}, {"Release", 1},
        {"AddAttachedSurface", 2}, {"AddOverlayDirtyRect", 2}, {"Blt", 6, s_Blt}, {"BltBatch", 4},
        {"BltFast", 6, s_BltFast}, {"DeleteAttachedSurface", 3}, {"EnumAttachedSurfaces", 3},
        {"EnumOverlayZOrders", 4}, {"Flip", 3, s_Flip}, {"GetAttachedSurface", 3}, {"GetBltStatus", 2},
        {"GetCaps", 2}, {"GetClipper", 2}, {"GetColorKey", 3}, {"GetDC", 2, s_GetDC},
        {"GetFlipStatus", 2}, {"GetOverlayPosition", 3}, {"GetPalette", 2},
        {"GetPixelFormat", 2, s_GetPixelFormat}, {"GetSurfaceDesc", 2, s_GetSurfaceDesc},
        {"Initialize", 3}, {"IsLost", 1}, {"Lock", 5, s_Lock}, {"ReleaseDC", 2, s_ReleaseDC},
        {"Restore", 1}, {"SetClipper", 2}, {"SetColorKey", 3, s_SetColorKey}, {"SetOverlayPosition", 3},
        {"SetPalette", 2}, {"Unlock", 2, s_Unlock}, {"UpdateOverlay", 6}, {"UpdateOverlayDisplay", 2},
        {"UpdateOverlayZOrder", 3}}));
    registerComKind("clip", build({{"QueryInterface", 3}, {"AddRef", 1}, {"Release", 1},
        {"GetClipList", 4}, {"GetHWnd", 2}, {"Initialize", 3}, {"IsClipListChanged", 2},
        {"SetClipList", 3}, {"SetHWnd", 3}}));
    registerComKind("ds", build({{"QueryInterface", 3}, {"AddRef", 1}, {"Release", 1},
        {"CreateSoundBuffer", 4, ds_CreateSoundBuffer}, {"GetCaps", 2},
        {"DuplicateSoundBuffer", 3, ds_DuplicateSoundBuffer}, {"SetCooperativeLevel", 3},
        {"Compact", 1}, {"GetSpeakerConfig", 2}, {"SetSpeakerConfig", 2}, {"Initialize", 2}}));
    registerComKind("graph", build({{"QueryInterface", 3, dshow_QueryInterface}, {"AddRef", 1},
        {"Release", 1}, {"AddFilter", 3}, {"RemoveFilter", 2}, {"EnumFilters", 2},
        {"FindFilterByName", 3}, {"ConnectDirect", 4}, {"Reconnect", 2}, {"Disconnect", 2},
        {"SetDefaultSyncSource", 1}, {"Connect", 3}, {"Render", 2}, {"RenderFile", 3, graph_RenderFile},
        {"AddSourceFilter", 4}, {"SetLogFile", 2}, {"Abort", 1}, {"ShouldOperationContinue", 1}}));
    registerComKind("mctrl", build({{"QueryInterface", 3, dshow_QueryInterface}, {"AddRef", 1},
        {"Release", 1}, DISPATCH_METHODS, {"Run", 1, mctrl_Run}, {"Pause", 1}, {"Stop", 1, mctrl_Stop},
        {"GetState", 3}, {"RenderFile", 2}, {"AddSourceFilter", 3}, {"get_FilterCollection", 2},
        {"get_RegFilterCollection", 2}, {"StopWhenReady", 1}}));
    registerComKind("mevent", build({{"QueryInterface", 3, dshow_QueryInterface}, {"AddRef", 1},
        {"Release", 1}, DISPATCH_METHODS, {"GetEventHandle", 2}, {"GetEvent", 5, mevent_GetEvent},
        {"WaitForCompletion", 3}, {"CancelDefaultHandling", 2}, {"RestoreDefaultHandling", 2},
        {"FreeEventParams", 4}, {"SetNotifyWindow", 4}, {"SetNotifyFlags", 2}, {"GetNotifyFlags", 2}}));
    registerComKind("mpos", build({{"QueryInterface", 3, dshow_QueryInterface}, {"AddRef", 1},
        {"Release", 1}, DISPATCH_METHODS, {"get_Duration", 2}, {"put_CurrentPosition", 3},
        {"get_CurrentPosition", 2}, {"get_StopTime", 2}, {"put_StopTime", 3}, {"get_PrerollTime", 2},
        {"put_PrerollTime", 3}, {"put_Rate", 3}, {"get_Rate", 2}, {"CanSeekForward", 2},
        {"CanSeekBackward", 2}}));
    registerComKind("baudio", build({{"QueryInterface", 3, dshow_QueryInterface}, {"AddRef", 1},
        {"Release", 1}, DISPATCH_METHODS, {"put_Volume", 2, baudio_put_Volume}, {"get_Volume", 2},
        {"put_Balance", 2}, {"get_Balance", 2}}));
    registerComKind("dsb", build({{"QueryInterface", 3}, {"AddRef", 1}, {"Release", 1}, {"GetCaps", 2},
        {"GetCurrentPosition", 3}, {"GetFormat", 4}, {"GetVolume", 2}, {"GetPan", 2},
        {"GetFrequency", 2}, {"GetStatus", 2, dsb_GetStatus}, {"Initialize", 3}, {"Lock", 8, dsb_Lock},
        {"Play", 4, dsb_Play}, {"SetCurrentPosition", 2}, {"SetFormat", 2}, {"SetVolume", 2, dsb_SetVolume},
        {"SetPan", 2, dsb_SetPan}, {"SetFrequency", 2}, {"Stop", 1, dsb_Stop}, {"Unlock", 5, dsb_Unlock},
        {"Restore", 1}}));
}

struct Binding { const char* name; rt::StubFn fn; };
extern const Binding kGdiBindings[] = {
    {"LoadImageA", g_LoadImageA}, {"GetObjectA", g_GetObjectA},
    {"CreateCompatibleDC", g_CreateCompatibleDC}, {"SelectObject", g_SelectObject},
    {"DeleteDC", g_DeleteDC}, {"DeleteObject", g_DeleteObject}, {"SetBkColor", g_SetBkColor},
    {"SetTextColor", g_SetTextColor}, {"TextOutA", g_TextOutA}, {"StretchBlt", g_StretchBlt},
    {nullptr, nullptr}};
extern const Binding kDxBindings[] = {
    {"DirectDrawCreate", x_DirectDrawCreate}, {"#1", x_DirectSoundCreate},
    {"CoCreateInstance", x_CoCreateInstance}, {nullptr, nullptr}};

}  // namespace win32
