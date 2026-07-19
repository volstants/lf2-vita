// ─────────────────────────────────────────────────────────────────────────────
//  test_dat — validates dat.hpp without needing the original LF2 files.
//
//  Build & run:
//    g++ -std=c++17 -I src tests/test_dat.cpp -o /tmp/test_dat && /tmp/test_dat
//
//  The sample below is written in the exact syntax of the original .dat files
//  (frames copied in shape from dennis.dat), encrypted in memory with the same
//  algorithm the game uses, then decrypted and parsed — a full round trip.
// ─────────────────────────────────────────────────────────────────────────────
#include "engine/dat.hpp"
#include <cstdio>
#include <cmath>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { std::printf("FAIL %s:%d  %s == %s  (got %ld, want %ld)\n", \
        __FILE__, __LINE__, #a, #b, (long)_a, (long)_b); ++failures; } \
} while (0)

static const char* SAMPLE = R"DAT(
<bmp_begin>
name: Dennis
head: sprite\sys\dennis_f.bmp
small: sprite\sys\dennis_s.bmp
file(0-69): sprite\sys\dennis_0.bmp  w: 79  h: 79  row: 10  col: 7
file(70-139): sprite\sys\dennis_1.bmp  w: 79  h: 79  row: 10  col: 7
walking_frame_rate 3
walking_speed 4.000000
walking_speedz 2.000000
running_frame_rate 3
running_speed 9.000000
running_speedz 1.800000
heavy_walking_speed 3.000000
jump_height -16.299999
jump_distance 8.000000
dash_height -6.000000
dash_distance 12.000000
rowing_height -2.000000
rowing_distance 4.000000
<bmp_end>

<frame> 0 standing
   pic: 0  state: 0  wait: 3  next: 1  dvx: 0  dvy: 0  dvz: 0  centerx: 39  centery: 79  hit_a: 0  hit_d: 0  hit_j: 0
   bdy:
      kind: 0  x: 25  y: 17  w: 30  h: 62
   bdy_end:
<frame_end>

<frame> 60 punch
   pic: 11  state: 3  wait: 2  next: 61  dvx: 0  dvy: 0  dvz: 0  centerx: 39  centery: 79  hit_a: 62  sound: data\001.wav
   itr:
      kind: 0  x: 42  y: 28  w: 40  h: 24  dvx: 5  dvy: -2  fall: 20  vrest: 7  arest: 4  bdefend: 12  injury: 10  effect: 0  zwidth: 12
   itr_end:
   bdy:
      kind: 0  x: 25  y: 17  w: 30  h: 62
   bdy_end:
   wpoint:
      kind: 1  x: 40  y: 40  weaponact: 21  attacking: 1  cover: 0  dvx: 0  dvy: 0  dvz: 0
   wpoint_end:
<frame_end>

<frame> 121 catch
   pic: 121  state: 9  wait: 1  next: 122  dvx: 0  dvy: 0  centerx: 39  centery: 79
   cpoint:
      kind: 1  x: 47  y: 33  vaction: 124  aaction: 130  throwvx: 8  throwvy: -9  throwinjury: 30  injury: 10  cover: 1
   cpoint_end:
   bdy:
      kind: 0  x: 25  y: 17  w: 30  h: 62
   bdy_end:
<frame_end>

<frame> 232 fireball
   pic: 232  state: 3  wait: 2  next: 233  dvx: 0  dvy: 0  centerx: 39  centery: 79  mp: 150
   opoint:
      kind: 1  x: 55  y: 40  action: 0  dvx: 0  dvy: 0  oid: 200  facing: 0
   opoint_end:
   bpoint:
      x: 40  y: 40
   bpoint_end:
<frame_end>
)DAT";

int main() {
    // ── Round trip: encrypt in memory, decrypt, compare ───────────────────────
    std::string plain(SAMPLE);
    auto enc = dat::encrypt(plain);
    CHECK_EQ(enc.size(), plain.size() + dat::DAT_JUNK);
    std::string dec = dat::decrypt(enc.data(), enc.size());
    CHECK(dec == plain);

    // Parsing decrypted bytes must equal parsing plaintext
    dat::File d  = dat::parse(dec);
    dat::File d2 = dat::parse(plain);
    CHECK_EQ(d.frames.size(), d2.frames.size());

    // ── Header ────────────────────────────────────────────────────────────────
    CHECK(d.header.name == "Dennis");
    CHECK(d.header.head == "sprite\\sys\\dennis_f.bmp");
    CHECK_EQ(d.header.files.size(), (size_t)2);
    CHECK_EQ(d.header.files[0].startPic, 0);
    CHECK_EQ(d.header.files[0].endPic,  69);
    CHECK_EQ(d.header.files[0].w,       79);
    CHECK_EQ(d.header.files[0].h,       79);
    CHECK_EQ(d.header.files[0].row,     10);
    CHECK_EQ(d.header.files[0].col,      7);
    CHECK(d.header.files[1].path == "sprite\\sys\\dennis_1.bmp");
    CHECK_EQ(d.header.files[1].startPic, 70);
    CHECK(std::fabs(d.header.get("walking_speed") - 4.0f)   < 1e-4f);
    CHECK(std::fabs(d.header.get("jump_height") + 16.2999f) < 1e-3f);
    CHECK(std::fabs(d.header.get("running_speed") - 9.0f)   < 1e-4f);
    CHECK_EQ((int)d.header.get("walking_frame_rate"), 3);

    // ── Frame count & indexing by id (not position) ───────────────────────────
    CHECK_EQ(d.frames.size(), (size_t)4);
    CHECK(d.frame(0)   != nullptr);
    CHECK(d.frame(60)  != nullptr);
    CHECK(d.frame(121) != nullptr);
    CHECK(d.frame(232) != nullptr);
    CHECK(d.frame(999) == nullptr);

    // ── Frame 0: standing ─────────────────────────────────────────────────────
    {
        const dat::Frame& f = *d.frame(0);
        CHECK(f.name == "standing");
        CHECK_EQ(f.pic, 0);   CHECK_EQ(f.state, 0);
        CHECK_EQ(f.wait, 3);  CHECK_EQ(f.next, 1);
        CHECK_EQ(f.centerx, 39); CHECK_EQ(f.centery, 79);
        CHECK_EQ(f.bdys.size(), (size_t)1);
        CHECK_EQ(f.bdys[0].x, 25); CHECK_EQ(f.bdys[0].y, 17);
        CHECK_EQ(f.bdys[0].w, 30); CHECK_EQ(f.bdys[0].h, 62);
        CHECK(f.itrs.empty());
    }

    // ── Frame 60: attack with itr + wpoint + sound + hit_a ────────────────────
    {
        const dat::Frame& f = *d.frame(60);
        CHECK(f.name == "punch");
        CHECK_EQ(f.pic, 11); CHECK_EQ(f.state, 3);
        CHECK_EQ(f.next, 61);
        CHECK_EQ(f.hit_a, 62);
        CHECK(f.sound == "data\\001.wav");
        CHECK_EQ(f.itrs.size(), (size_t)1);
        const dat::Itr& i = f.itrs[0];
        CHECK_EQ(i.kind, 0);
        CHECK_EQ(i.x, 42); CHECK_EQ(i.y, 28); CHECK_EQ(i.w, 40); CHECK_EQ(i.h, 24);
        CHECK_EQ(i.dvx, 5); CHECK_EQ(i.dvy, -2);
        CHECK_EQ(i.fall, 20); CHECK_EQ(i.vrest, 7); CHECK_EQ(i.arest, 4);
        CHECK_EQ(i.bdefend, 12); CHECK_EQ(i.injury, 10);
        CHECK_EQ(i.effect, 0); CHECK_EQ(i.zwidth, 12);
        // bdy must not be swallowed by the itr block
        CHECK_EQ(f.bdys.size(), (size_t)1);
        CHECK_EQ(f.bdys[0].w, 30);
        CHECK_EQ(f.wpoints.size(), (size_t)1);
        CHECK_EQ(f.wpoints[0].weaponact, 21);
        CHECK_EQ(f.wpoints[0].attacking, 1);
    }

    // ── Frame 121: cpoint with open-ended fields ──────────────────────────────
    {
        const dat::Frame& f = *d.frame(121);
        CHECK_EQ(f.cpoints.size(), (size_t)1);
        const dat::Cpoint& c = f.cpoints[0];
        CHECK_EQ(c.kind, 1); CHECK_EQ(c.x, 47); CHECK_EQ(c.y, 33);
        CHECK_EQ(c.get("vaction"),     124);
        CHECK_EQ(c.get("aaction"),     130);
        CHECK_EQ(c.get("throwvx"),       8);
        CHECK_EQ(c.get("throwvy"),      -9);
        CHECK_EQ(c.get("throwinjury"),  30);
        CHECK_EQ(c.get("cover"),         1);
        CHECK_EQ(c.get("missing", -7),  -7);
        CHECK_EQ(f.bdys.size(), (size_t)1);
    }

    // ── Frame 232: opoint + bpoint + mp ───────────────────────────────────────
    {
        const dat::Frame& f = *d.frame(232);
        CHECK_EQ(f.mp, 150);
        CHECK_EQ(f.opoints.size(), (size_t)1);
        CHECK_EQ(f.opoints[0].oid, 200);
        CHECK_EQ(f.opoints[0].kind, 1);
        CHECK_EQ(f.opoints[0].x, 55);
        CHECK_EQ(f.bpoints.size(), (size_t)1);
        CHECK_EQ(f.bpoints[0].x, 40);
        CHECK_EQ(f.bpoints[0].y, 40);
    }

    // ── loadText auto-detect: plaintext file must not be "decrypted" ──────────
    {
        const char* tmp = "/tmp/_lf2_plain_test.dat";
        FILE* fp = std::fopen(tmp, "wb");
        CHECK(fp != nullptr);
        if (fp) {
            const char* head = "<bmp_begin>\nname: Test\n<bmp_end>\n";
            std::fwrite(head, 1, std::strlen(head), fp);
            std::fclose(fp);
            dat::File p = dat::load(tmp);
            CHECK(p.header.name == "Test");
            std::remove(tmp);
        }
    }

    // ── Encrypted file on disk ────────────────────────────────────────────────
    {
        const char* tmp = "/tmp/_lf2_enc_test.dat";
        FILE* fp = std::fopen(tmp, "wb");
        CHECK(fp != nullptr);
        if (fp) {
            std::fwrite(enc.data(), 1, enc.size(), fp);
            std::fclose(fp);
            dat::File p = dat::load(tmp);
            CHECK(p.header.name == "Dennis");
            CHECK_EQ(p.frames.size(), (size_t)4);
            CHECK_EQ(p.frame(60)->itrs[0].injury, 10);
            std::remove(tmp);
        }
    }

    // ── Optional: real LF2 data ───────────────────────────────────────────────
    // Skipped when data/ is absent, so the suite runs on a clean checkout.
    // Values below were read off the original files and are regression anchors.
    {
        dat::File dennis = dat::load("data/dennis.dat");
        if (dennis.frames.empty()) {
            std::printf("skip: data/dennis.dat not present "
                        "(drop the original LF2 data/ folder here to enable)\n");
        } else {
            std::printf("running against real LF2 data\n");
            CHECK(dennis.header.name == "Dennis");
            CHECK_EQ(dennis.frames.size(), (size_t)214);
            CHECK_EQ(dennis.header.files.size(), (size_t)3);
            // 3 sheets of 70 pics each (row 10 × col 7)
            CHECK_EQ(dennis.header.files[0].startPic,   0);
            CHECK_EQ(dennis.header.files[1].startPic,  70);
            CHECK_EQ(dennis.header.files[2].startPic, 140);
            CHECK_EQ(dennis.header.files[0].row, 10);
            CHECK_EQ(dennis.header.files[0].col,  7);
            CHECK(std::fabs(dennis.header.get("running_speed") - 10.5f) < 1e-4f);
            CHECK(std::fabs(dennis.header.get("jump_height") + 16.3f)   < 1e-3f);

            // sheetOf + pixelOf: pic 140 is the first image of dennis_2.bmp
            const dat::SpriteSheet* sh = dennis.header.sheetOf(140);
            CHECK(sh != nullptr);
            if (sh) {
                CHECK(sh->path.find("dennis_2") != std::string::npos);
                int sx = -1, sy = -1;
                sh->pixelOf(140, sx, sy);
                CHECK_EQ(sx, 0); CHECK_EQ(sy, 0);
                sh->pixelOf(151, sx, sy);          // local 11 → row 1, col 1
                CHECK_EQ(sx, sh->w); CHECK_EQ(sy, sh->h);
            }
            // A pic in the middle of the sheet must not be attributed to sheet 0
            CHECK(dennis.header.sheetOf(69)->path.find("dennis_0") != std::string::npos);
            CHECK(dennis.header.sheetOf(70)->path.find("dennis_1") != std::string::npos);

            // Standing frame invariants shared by every character
            const dat::Frame* f0 = dennis.frame(0);
            CHECK(f0 != nullptr);
            if (f0) { CHECK(f0->name == "standing"); CHECK_EQ(f0->bdys.size(), (size_t)1); }

            // Object-type file: weapon sounds are strings, not numbers
            dat::File ball = dat::load("data/john_ball.dat");
            CHECK(!ball.frames.empty());
            CHECK(ball.header.str("weapon_hit_sound").find(".wav") != std::string::npos);
            CHECK_EQ(ball.header.files.size(), (size_t)1);

            // Object index resolves oid → file (opoints reference these ids)
            dat::Index ix = dat::loadIndex("data/data.txt");
            CHECK(ix.objects.size() > 40);
            CHECK(!ix.backgrounds.empty());
            const dat::ObjectEntry* deep = ix.object(1);
            CHECK(deep != nullptr);
            if (deep) {
                CHECK_EQ(deep->type, 0);
                CHECK(deep->file.find("deep.dat") != std::string::npos);
            }
            // Comment stripping: "id: 213 file: data\weapon7.dat  #ice_sword"
            const dat::ObjectEntry* ice = ix.object(213);
            CHECK(ice != nullptr);
            if (ice) {
                CHECK(ice->file.find("weapon7.dat") != std::string::npos);
                CHECK(ice->file.find('#') == std::string::npos);
            }
            // Every opoint in a character must resolve to a known object
            for (const auto& fr : dennis.frames)
                for (const auto& op : fr.opoints)
                    if (!ix.object(op.oid))
                        { std::printf("FAIL dennis frame %d: oid %d unknown\n",
                                      fr.id, op.oid); ++failures; }
        }
    }

    if (failures == 0) std::printf("all tests passed\n");
    else               std::printf("%d test(s) failed\n", failures);
    return failures ? 1 : 0;
}
