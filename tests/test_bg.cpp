// ─────────────────────────────────────────────────────────────────────────────
//  test_bg — validates the bg.dat parser (dat::parseBackground) + the parallax
//  math, without needing the original LF2 files.
//
//  Build & run:
//    g++ -std=c++17 -I src tests/test_bg.cpp -o /tmp/test_bg && /tmp/test_bg
//
//  The SAMPLE is Lion Forest's real bg.dat, shrunk to the layers that exercise
//  every field path: opaque tiled (forests), transparent single (forestm),
//  looped/transparent (forestt), a rect solid-fill (s.bmp) and looped ground
//  (land1). Encrypted in memory with the game's algorithm, then decrypted and
//  parsed — a full round trip, same shape as test_dat.
// ─────────────────────────────────────────────────────────────────────────────
#include "engine/dat.hpp"
#include <cstdio>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { std::printf("FAIL %s:%d  %s == %s  (got %ld, want %ld)\n", \
        __FILE__, __LINE__, #a, #b, (long)_a, (long)_b); ++failures; } \
} while (0)

static const char* SAMPLE = R"BG(
name: Lion_Forest
width: 3200   zboundary: 365 505
shadow: bg\sys\lf\s.bmp  shadowsize: 37 9
layer:
  bg\sys\lf\forests.bmp
  transparency: 0  width: 800  x: 0  y: 128
layer_end
layer:
  bg\sys\lf\forestm1.bmp
  transparency: 1 width: 1100  x: 0  y: 147
layer_end
layer:
  bg\sys\lf\forestt.bmp
  transparency: 1 width: 2900  x: 0  y: 199  loop: 253
layer_end
layer:
  bg\sys\lf\s.bmp
  rect: 4706 x: 0  y: 356  width: 794  height: 172
layer_end
layer:
  bg\sys\lf\land1.bmp
  transparency: 0 width: 2950   x: 0  y: 356 loop: 520
layer_end
)BG";

int main() {
    // ── Round trip: encrypt in memory, decrypt, parse == parse(plain) ─────────
    std::string plain(SAMPLE);
    auto enc = dat::encrypt(plain);
    std::string dec = dat::decrypt(enc.data(), enc.size());
    CHECK(dec == plain);

    dat::Background bg = dat::parseBackground(dec);
    CHECK(bg.ok);

    // ── Scene header ──────────────────────────────────────────────────────────
    CHECK(bg.name == "Lion Forest");     // '_' → ' ' like the loader
    CHECK_EQ(bg.width, 3200);
    CHECK_EQ(bg.zTop,   365);
    CHECK_EQ(bg.zBottom, 505);
    CHECK(bg.shadow == "bg\\sys\\lf\\s.bmp");
    CHECK_EQ(bg.shadowW, 37);
    CHECK_EQ(bg.shadowH,  9);
    CHECK_EQ(bg.layers.size(), (size_t)5);

    // ── forests: opaque, drawn once (no loop) ─────────────────────────────────
    {
        const dat::BgLayer& L = bg.layers[0];
        CHECK(L.file == "bg\\sys\\lf\\forests.bmp");
        CHECK_EQ(L.transparency, 0);
        CHECK_EQ(L.width, 800);
        CHECK_EQ(L.x, 0); CHECK_EQ(L.y, 128);
        CHECK_EQ(L.loop, 0);
        CHECK(!L.isRect());
    }

    // ── forestm1: transparent (colour-key), single ───────────────────────────
    {
        const dat::BgLayer& L = bg.layers[1];
        CHECK_EQ(L.transparency, 1);
        CHECK_EQ(L.width, 1100);
        CHECK_EQ(L.y, 147);
        CHECK_EQ(L.loop, 0);
    }

    // ── forestt: transparent, looped/tiled ────────────────────────────────────
    {
        const dat::BgLayer& L = bg.layers[2];
        CHECK_EQ(L.transparency, 1);
        CHECK_EQ(L.width, 2900);
        CHECK_EQ(L.loop, 253);
        CHECK(!L.isRect());
    }

    // ── s.bmp: rect solid-fill (RGB565 → dark green) ──────────────────────────
    {
        const dat::BgLayer& L = bg.layers[3];
        CHECK(L.isRect());
        CHECK_EQ(L.rect, 4706L);
        CHECK_EQ(L.x, 0); CHECK_EQ(L.y, 356);
        CHECK_EQ(L.width, 794);   // for rect layers width/height are the box size
        CHECK_EQ(L.height, 172);
        int r, g, b; dat::rectColor(L.rect, r, g, b);
        CHECK_EQ(r, 16); CHECK_EQ(g, 76); CHECK_EQ(b, 16);
    }

    // ── land1: opaque, looped ground ──────────────────────────────────────────
    {
        const dat::BgLayer& L = bg.layers[4];
        CHECK_EQ(L.transparency, 0);
        CHECK_EQ(L.width, 2950);
        CHECK_EQ(L.loop, 520);
    }

    // ── Parallax math (faithful to draw routine FUN_0041a250) ─────────────────
    // A layer whose width == bgWidth tracks the camera 1:1 (foreground/ground).
    {
        dat::BgLayer ground; ground.x = 0; ground.width = bg.width;  // 3200
        CHECK_EQ(bg.layerScreenX(ground, 0),    0);
        CHECK_EQ(bg.layerScreenX(ground, 500), -500);
        CHECK_EQ(bg.layerScreenX(ground, 1000),-1000);
    }
    // A layer whose width == PARALLAX_REF (794) stays fixed regardless of camera.
    {
        dat::BgLayer fixed; fixed.x = 40; fixed.width = dat::Background::PARALLAX_REF;
        CHECK_EQ(bg.layerScreenX(fixed, 0),    40);
        CHECK_EQ(bg.layerScreenX(fixed, 900),  40);
    }
    // forests (width 800) barely moves: off = (800-794)*camX/(3200-794).
    {
        const dat::BgLayer& L = bg.layers[0];
        CHECK_EQ(bg.layerScreenX(L, 2406), L.x - 6);   // (6*2406)/2406 = 6
    }

    // ── Optional: the real Lion Forest bg.dat on disk ─────────────────────────
    {
        dat::Background real = dat::loadBackground("bg/sys/lf/bg.dat");
        if (!real.ok) {
            std::printf("skip: bg/sys/lf/bg.dat not present\n");
        } else {
            std::printf("running against real Lion Forest bg.dat\n");
            CHECK(real.name == "Lion Forest");
            CHECK_EQ(real.width, 3200);
            CHECK_EQ(real.zTop, 365);
            CHECK_EQ(real.zBottom, 505);
            // Real file has 10 layers (6 forest + rect + 3 land).
            CHECK_EQ(real.layers.size(), (size_t)10);
            // Last layer is land4, the fastest-scrolling ground element.
            const dat::BgLayer& last = real.layers.back();
            CHECK(last.file.find("land4") != std::string::npos);
            CHECK_EQ(last.width, 3200);
            CHECK_EQ(last.loop, 570);
            // Exactly one rect layer (the ground fill).
            int rects = 0;
            for (const auto& L : real.layers) if (L.isRect()) ++rects;
            CHECK_EQ(rects, 1);
        }
    }

    if (failures == 0) std::printf("all tests passed\n");
    else               std::printf("%d test(s) failed\n", failures);
    return failures ? 1 : 0;
}
