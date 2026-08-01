#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  dat.hpp — LF2 .dat file decryption + parsing
//
//  Pure C++ (no SDL / Vita deps) so it compiles on host for testing.
//  Reads original Little Fighter 2 data files and produces generic
//  structures the engine interprets at runtime.
//
//  Format notes:
//   • Encrypted files: 123 junk bytes, then each byte is
//     plain = enc - KEY[i % strlen(KEY)]  (cyclic caesar with known password).
//   • Plaintext body: <bmp_begin>…<bmp_end> header, then <frame> blocks.
//   • wait/next values count ORIGINAL 30fps ticks (TICK ≈ 33ms). The engine
//     must run logic at 30Hz (or scale waits) when consuming this data.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include "log.hpp"

namespace dat {

// ── Decryption ────────────────────────────────────────────────────────────────
inline const char* DAT_KEY     = "odBearBecauseHeIsVeryGoodSiuHungIsAGo";
constexpr size_t   DAT_JUNK    = 123;   // junk header bytes in encrypted files

inline std::string decrypt(const uint8_t* buf, size_t n) {
    std::string out;
    if (n <= DAT_JUNK) return out;
    const size_t klen = std::strlen(DAT_KEY);
    out.reserve(n - DAT_JUNK);
    for (size_t i = DAT_JUNK; i < n; ++i)
        out.push_back(char(uint8_t(buf[i]) - uint8_t(DAT_KEY[(i - DAT_JUNK) % klen])));
    return out;
}

// Inverse — used only by tests / tooling.
inline std::vector<uint8_t> encrypt(const std::string& plain) {
    std::vector<uint8_t> out(DAT_JUNK, 0);
    const size_t klen = std::strlen(DAT_KEY);
    out.reserve(DAT_JUNK + plain.size());
    for (size_t i = 0; i < plain.size(); ++i)
        out.push_back(uint8_t(plain[i]) + uint8_t(DAT_KEY[i % klen]));
    return out;
}

// Load a .dat from disk; auto-detects plaintext vs encrypted.
inline std::string loadText(const char* path) {
    FILE* f = std::fopen(path, "rb");
    // Antes: `return {}` mudo. Um .dat ausente produzia um File sem frames e o
    // jogo seguia com um personagem que não anima nem colide.
    LF2_CHECK(f, {}, "nao abriu: %s", path);
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)n);
    if (n > 0 && std::fread(buf.data(), 1, (size_t)n, f) != (size_t)n) { std::fclose(f); return {}; }
    std::fclose(f);
    // Plaintext files (data.txt, stage.dat in some releases) open with a tag
    // like "<object>" / "<bmp_begin>"; encrypted ones open with 123 junk bytes.
    if (n > 4 && buf[0] == '<' && std::isalpha(buf[1])) {
        size_t look = (size_t)n < 64 ? (size_t)n : 64, printable = 0;
        for (size_t i = 0; i < look; ++i)
            if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t' ||
                (buf[i] >= 32 && buf[i] < 127)) ++printable;
        if (printable == look) return std::string((const char*)buf.data(), (size_t)n);
    }
    return decrypt(buf.data(), (size_t)n);
}

// ── Data structures ──────────────────────────────────────────────────────────
// file(0-69): sprite\sys\davis_0.bmp  w: 79  h: 79  row: 10  col: 7
//
// TRAP: despite the names, `row` is the number of pictures ACROSS (columns in
// the image) and `col` is the number of picture ROWS. row*col = sheet capacity
// (10*7 = 70 pics). Use pixelOf() below instead of doing the math by hand.
struct SpriteSheet {
    std::string path;
    int startPic = 0, endPic = 0;
    int w = 0, h = 0, row = 0, col = 0;

    bool contains(int pic) const { return pic >= startPic && pic <= endPic; }
    int  localPic(int pic)  const { return pic - startPic; }
    // Source rect of `pic` inside this sheet (pic is the GLOBAL index).
    void pixelOf(int pic, int& sx, int& sy) const {
        int l = localPic(pic);
        sx = (row > 0 ? l % row : 0) * w;
        sy = (row > 0 ? l / row : 0) * h;
    }
};

struct Header {
    std::string name, head, small;
    std::vector<SpriteSheet> files;
    // Numeric stats: walking_speed, jump_height, weapon_hp, …
    std::unordered_map<std::string, float> vals;
    // String stats: weapon_hit_sound, weapon_drop_sound, weapon_broken_sound
    std::unordered_map<std::string, std::string> strs;

    float get(const char* k, float def = 0.f) const {
        auto it = vals.find(k);
        return it == vals.end() ? def : it->second;
    }
    std::string str(const char* k, const char* def = "") const {
        auto it = strs.find(k);
        return it == strs.end() ? std::string(def) : it->second;
    }
    // Which sheet holds this pic? nullptr if none declares it.
    const SpriteSheet* sheetOf(int pic) const {
        for (const auto& s : files) if (s.contains(pic)) return &s;
        return nullptr;
    }
};

struct Itr {
    int kind = 0, x = 0, y = 0, w = 0, h = 0;
    int dvx = 0, dvy = 0, fall = -1, arest = 0, vrest = 0;
    int bdefend = 0, injury = 0, effect = -1, zwidth = -1;
    int catchingact[2] = { -1, -1 };
    int caughtact[2]   = { -1, -1 };
};

struct Bdy    { int kind = 0, x = 0, y = 0, w = 0, h = 0; };
struct Wpoint { int kind = 0, x = 0, y = 0, weaponact = 0, attacking = 0,
                    cover = 0, dvx = 0, dvy = 0, dvz = 0; };
struct Opoint { int kind = 0, x = 0, y = 0, action = 0, dvx = 0, dvy = 0,
                    oid = 0, facing = 0; };
struct Bpoint { int x = 0, y = 0; };

// Catch point. Field set varies wildly between characters (vaction, aaction,
// taction, throwvx/vy/vz, throwinjury, hurtable, decrease, dircontrol,
// fronthurtact, backhurtact, cover, injury…), so they live in a map.
//
// NOTE: the original files contain -842150451 (0xCDCDCDCD, MSVC's
// uninitialized-memory fill) in several throwv*/throwinjury fields. Treat that
// value as "unset", not as a real number — see isUnset().
struct Cpoint {
    int kind = 0, x = 0, y = 0;
    std::unordered_map<std::string, int> vals;
    int get(const char* k, int def = 0) const {
        auto it = vals.find(k);
        return it == vals.end() ? def : it->second;
    }
    bool has(const char* k) const { return vals.find(k) != vals.end(); }
};

constexpr int UNSET = -842150451;   // 0xCDCDCDCD leaked into the original data
inline bool isUnset(int v) { return v == UNSET; }

struct Frame {
    int id = -1;
    std::string name;
    int pic = 0, state = 0, wait = 0, next = 0;
    int dvx = 0, dvy = 0, dvz = 0;
    int centerx = 0, centery = 0, mp = 0;
    // hit_* : frame jumps on input (a=attack, j=jump, d=defend, F/U/D=dir)
    int hit_a = 0, hit_d = 0, hit_j = 0;
    int hit_Fa = 0, hit_Ua = 0, hit_Da = 0;
    int hit_Fj = 0, hit_Uj = 0, hit_Dj = 0, hit_ja = 0;
    std::string sound;
    std::vector<Itr>    itrs;
    std::vector<Bdy>    bdys;
    std::vector<Wpoint> wpoints;
    std::vector<Opoint> opoints;
    std::vector<Cpoint> cpoints;
    std::vector<Bpoint> bpoints;
};

// <weapon_strength_list> entries: the REAL damage of a held weapon's swing.
// Selected by the holder's wpoint.attacking (1 normal · 2 jump · 3 run · 4 dash).
struct StrengthEntry {
    int dvx = 0, dvy = 0, fall = 20, vrest = 0, arest = 0,
        bdefend = 0, injury = 0, effect = 0;
    bool valid = false;
};

struct File {
    Header header;
    StrengthEntry strength[8];   // index = entry id (1..7)
    std::vector<Frame> frames;                    // in file order
    std::unordered_map<int, int> frameIndex;      // frame id → index in frames
    const Frame* frame(int id) const {
        auto it = frameIndex.find(id);
        return it == frameIndex.end() ? nullptr : &frames[(size_t)it->second];
    }
};

// ── Tokenizer ────────────────────────────────────────────────────────────────
namespace detail {

struct Tok {
    const std::string& s;
    size_t i = 0;
    explicit Tok(const std::string& text) : s(text) {}

    bool eof() { skipWs(); return i >= s.size(); }

    // Skips whitespace AND '#' comments (used by data.txt and stage.dat:
    // "id: 213  file: data\weapon7.dat   #ice_sword").
    void skipWs() {
        for (;;) {
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
            if (i < s.size() && s[i] == '#') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            return;
        }
    }

    std::string next() {
        skipWs();
        size_t b = i;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) ++i;
        return s.substr(b, i - b);
    }

    std::string peek() {
        size_t save = i;
        std::string t = next();
        i = save;
        return t;
    }

    // Rest of current line, trimmed, comment stripped.
    // (for name:/head:/small: fields, whose values may contain spaces)
    std::string restOfLine() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t b = i;
        while (i < s.size() && s[i] != '\n' && s[i] != '\r') ++i;
        size_t e = i;
        for (size_t k = b; k < e; ++k) if (s[k] == '#') { e = k; break; }
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    int   nextInt()   { return std::atoi(next().c_str()); }
    float nextFloat() { return (float)std::atof(next().c_str()); }
};

inline bool isKey(const std::string& t) {
    return !t.empty() && t.back() == ':';
}
inline std::string keyName(const std::string& t) {
    return t.substr(0, t.size() - 1);
}

// Parse "kind: 0 x: 21 …" fields until terminator token (e.g. "bdy_end:")
template <typename SetFn>
inline void parseFields(Tok& tk, const char* endTag, SetFn set) {
    while (!tk.eof()) {
        std::string t = tk.peek();
        if (t == endTag || t == "<frame_end>" || t == "<frame>") return;
        tk.next();
        if (isKey(t)) set(keyName(t), tk);
        // non-key stray tokens are skipped
    }
}

} // namespace detail

// ── Parser ───────────────────────────────────────────────────────────────────
inline File parse(const std::string& text) {
    using namespace detail;
    File out;
    Tok tk(text);

    while (!tk.eof()) {
        std::string t = tk.next();

        // ── Header block ──────────────────────────────────────────────────────
        if (t == "<bmp_begin>") {
            while (!tk.eof()) {
                std::string h = tk.next();
                if (h == "<bmp_end>") break;

                if (h == "name:")       { out.header.name  = tk.restOfLine(); }
                else if (h == "head:")  { out.header.head  = tk.restOfLine(); }
                else if (h == "small:") { out.header.small = tk.restOfLine(); }
                else if (h.rfind("file(", 0) == 0) {
                    // file(0-69): path w: 79 h: 79 row: 10 col: 7
                    SpriteSheet sh;
                    std::sscanf(h.c_str(), "file(%d-%d):", &sh.startPic, &sh.endPic);
                    sh.path = tk.next();
                    for (int k = 0; k < 4; ++k) {
                        std::string kk = tk.next();       // w: / h: / row: / col:
                        int v = tk.nextInt();
                        if      (kk == "w:")   sh.w   = v;
                        else if (kk == "h:")   sh.h   = v;
                        else if (kk == "row:") sh.row = v;
                        else if (kk == "col:") sh.col = v;
                    }
                    out.header.files.push_back(sh);
                }
                else if (isKey(h)) {
                    // weapon_hit_sound / weapon_drop_sound / weapon_broken_sound
                    // carry file paths, not numbers.
                    std::string k = keyName(h);
                    if (k.size() > 6 && k.compare(k.size() - 6, 6, "_sound") == 0)
                        out.header.strs[k] = tk.next();
                    else
                        out.header.vals[k] = tk.nextFloat();
                }
                else {
                    // "walking_speed 4.0" style (no colon) — also seen in docs
                    out.header.vals[h] = tk.nextFloat();
                }
            }
            continue;
        }

        // ── Frame block ───────────────────────────────────────────────────────
        if (t == "<frame>") {
            Frame fr;
            fr.id   = tk.nextInt();
            fr.name = tk.restOfLine();

            while (!tk.eof()) {
                std::string k = tk.next();
                if (k == "<frame_end>") break;

                if (k == "bdy:") {
                    Bdy b;
                    parseFields(tk, "bdy_end:", [&](const std::string& kk, Tok& v) {
                        if      (kk == "kind") b.kind = v.nextInt();
                        else if (kk == "x")    b.x    = v.nextInt();
                        else if (kk == "y")    b.y    = v.nextInt();
                        else if (kk == "w")    b.w    = v.nextInt();
                        else if (kk == "h")    b.h    = v.nextInt();
                        else                   v.next();
                    });
                    if (tk.peek() == "bdy_end:") tk.next();
                    fr.bdys.push_back(b);
                }
                else if (k == "itr:") {
                    Itr it;
                    parseFields(tk, "itr_end:", [&](const std::string& kk, Tok& v) {
                        if      (kk == "kind")    it.kind    = v.nextInt();
                        else if (kk == "x")       it.x       = v.nextInt();
                        else if (kk == "y")       it.y       = v.nextInt();
                        else if (kk == "w")       it.w       = v.nextInt();
                        else if (kk == "h")       it.h       = v.nextInt();
                        else if (kk == "dvx")     it.dvx     = v.nextInt();
                        else if (kk == "dvy")     it.dvy     = v.nextInt();
                        else if (kk == "fall")    it.fall    = v.nextInt();
                        else if (kk == "arest")   it.arest   = v.nextInt();
                        else if (kk == "vrest")   it.vrest   = v.nextInt();
                        else if (kk == "bdefend") it.bdefend = v.nextInt();
                        else if (kk == "injury")  it.injury  = v.nextInt();
                        else if (kk == "effect")  it.effect  = v.nextInt();
                        else if (kk == "zwidth")  it.zwidth  = v.nextInt();
                        else if (kk == "catchingact") { it.catchingact[0] = v.nextInt();
                                                        it.catchingact[1] = v.nextInt(); }
                        else if (kk == "caughtact")   { it.caughtact[0]   = v.nextInt();
                                                        it.caughtact[1]   = v.nextInt(); }
                        else                      v.next();
                    });
                    if (tk.peek() == "itr_end:") tk.next();
                    fr.itrs.push_back(it);
                }
                else if (k == "wpoint:") {
                    Wpoint w;
                    parseFields(tk, "wpoint_end:", [&](const std::string& kk, Tok& v) {
                        if      (kk == "kind")      w.kind      = v.nextInt();
                        else if (kk == "x")         w.x         = v.nextInt();
                        else if (kk == "y")         w.y         = v.nextInt();
                        else if (kk == "weaponact") w.weaponact = v.nextInt();
                        else if (kk == "attacking") w.attacking = v.nextInt();
                        else if (kk == "cover")     w.cover     = v.nextInt();
                        else if (kk == "dvx")       w.dvx       = v.nextInt();
                        else if (kk == "dvy")       w.dvy       = v.nextInt();
                        else if (kk == "dvz")       w.dvz       = v.nextInt();
                        else                        v.next();
                    });
                    if (tk.peek() == "wpoint_end:") tk.next();
                    fr.wpoints.push_back(w);
                }
                else if (k == "opoint:") {
                    Opoint o;
                    parseFields(tk, "opoint_end:", [&](const std::string& kk, Tok& v) {
                        if      (kk == "kind")   o.kind   = v.nextInt();
                        else if (kk == "x")      o.x      = v.nextInt();
                        else if (kk == "y")      o.y      = v.nextInt();
                        else if (kk == "action") o.action = v.nextInt();
                        else if (kk == "dvx")    o.dvx    = v.nextInt();
                        else if (kk == "dvy")    o.dvy    = v.nextInt();
                        else if (kk == "oid")    o.oid    = v.nextInt();
                        else if (kk == "facing") o.facing = v.nextInt();
                        else                     v.next();
                    });
                    if (tk.peek() == "opoint_end:") tk.next();
                    fr.opoints.push_back(o);
                }
                else if (k == "cpoint:") {
                    Cpoint c;
                    parseFields(tk, "cpoint_end:", [&](const std::string& kk, Tok& v) {
                        if      (kk == "kind") c.kind = v.nextInt();
                        else if (kk == "x")    c.x    = v.nextInt();
                        else if (kk == "y")    c.y    = v.nextInt();
                        else                   c.vals[kk] = v.nextInt();
                    });
                    if (tk.peek() == "cpoint_end:") tk.next();
                    fr.cpoints.push_back(c);
                }
                else if (k == "bpoint:") {
                    Bpoint b;
                    parseFields(tk, "bpoint_end:", [&](const std::string& kk, Tok& v) {
                        if      (kk == "x") b.x = v.nextInt();
                        else if (kk == "y") b.y = v.nextInt();
                        else                v.next();
                    });
                    if (tk.peek() == "bpoint_end:") tk.next();
                    fr.bpoints.push_back(b);
                }
                else if (k == "sound:") { fr.sound = tk.next(); }
                else if (detail::isKey(k)) {
                    std::string kk = detail::keyName(k);
                    int v = tk.nextInt();
                    if      (kk == "pic")     fr.pic     = v;
                    else if (kk == "state")   fr.state   = v;
                    else if (kk == "wait")    fr.wait    = v;
                    else if (kk == "next")    fr.next    = v;
                    else if (kk == "dvx")     fr.dvx     = v;
                    else if (kk == "dvy")     fr.dvy     = v;
                    else if (kk == "dvz")     fr.dvz     = v;
                    else if (kk == "centerx") fr.centerx = v;
                    else if (kk == "centery") fr.centery = v;
                    else if (kk == "mp")      fr.mp      = v;
                    else if (kk == "hit_a")   fr.hit_a   = v;
                    else if (kk == "hit_d")   fr.hit_d   = v;
                    else if (kk == "hit_j")   fr.hit_j   = v;
                    else if (kk == "hit_Fa")  fr.hit_Fa  = v;
                    else if (kk == "hit_Ua")  fr.hit_Ua  = v;
                    else if (kk == "hit_Da")  fr.hit_Da  = v;
                    else if (kk == "hit_Fj")  fr.hit_Fj  = v;
                    else if (kk == "hit_Uj")  fr.hit_Uj  = v;
                    else if (kk == "hit_Dj")  fr.hit_Dj  = v;
                    else if (kk == "hit_ja")  fr.hit_ja  = v;
                    // unknown keys: value already consumed, ignore
                }
                // stray non-key tokens: skip
            }
            out.frameIndex[fr.id] = (int)out.frames.size();
            out.frames.push_back(std::move(fr));
            continue;
        }

        // ── <weapon_strength_list> ────────────────────────────────────────────
        //   entry: 1 normal
        //     dvx: 2  fall: 40  vrest: 10  bdefend: 16  injury: 45  effect: 1
        if (t == "<weapon_strength_list>") {
            int cur = -1;
            while (!tk.eof()) {
                std::string k = tk.next();
                if (k == "<weapon_strength_list_end>") break;
                if (k == "entry:") {
                    cur = tk.nextInt();
                    tk.restOfLine();                     // the name (normal/jump/…)
                    if (cur >= 0 && cur < 8) out.strength[cur].valid = true;
                    continue;
                }
                if (cur < 0 || cur >= 8 || !isKey(k)) continue;
                StrengthEntry& se = out.strength[cur];
                std::string kk = keyName(k);
                int v = tk.nextInt();
                if      (kk == "dvx")     se.dvx     = v;
                else if (kk == "dvy")     se.dvy     = v;
                else if (kk == "fall")    se.fall    = v;
                else if (kk == "vrest")   se.vrest   = v;
                else if (kk == "arest")   se.arest   = v;
                else if (kk == "bdefend") se.bdefend = v;
                else if (kk == "injury")  se.injury  = v;
                else if (kk == "effect")  se.effect  = v;
            }
            continue;
        }

        // ── Other top-level blocks — skip ────────────────────────────────────
    }
    return out;
}

// Convenience: load + parse in one call.
inline File load(const char* path) { return parse(loadText(path)); }

// ── Object index (data.txt) ──────────────────────────────────────────────────
//  <object> id: 1  type: 0  file: data\deep.dat … <object_end>
//  <background> id: 4  file: bg\sys\hkc\bg.dat  … <background_end>
//
//  type 0 = character, 1 = light weapon, 2 = heavy weapon, 3 = special effect
//  (projectiles/chasers), 4 = throwable, 5 = misc (etc.dat, broken_weapon.dat)
//
//  This is what resolves an opoint's `oid` to an actual data file, so the
//  engine can spawn Davis's ball or Henry's arrow without hardcoding ids.
struct ObjectEntry     { int id = 0, type = 0; std::string file; };
struct BackgroundEntry { int id = 0;           std::string file; };

struct Index {
    std::vector<ObjectEntry>     objects;
    std::vector<BackgroundEntry> backgrounds;
    std::unordered_map<int, int> byId;    // object id → index into objects

    const ObjectEntry* object(int id) const {
        auto it = byId.find(id);
        return it == byId.end() ? nullptr : &objects[(size_t)it->second];
    }
};

inline Index parseIndex(const std::string& text) {
    using namespace detail;
    Index ix;
    Tok tk(text);
    int section = 0;   // 0 none, 1 object, 2 background

    while (!tk.eof()) {
        std::string t = tk.next();
        if (t == "<object>")           { section = 1; continue; }
        if (t == "<object_end>")       { section = 0; continue; }
        if (t == "<background>")       { section = 2; continue; }
        if (t == "<background_end>")   { section = 0; continue; }
        if (section == 0 || t != "id:") continue;

        int id = tk.nextInt();
        int type = 0;
        std::string file;
        // Remaining fields on this entry: "type: 0  file: data\deep.dat"
        while (!tk.eof()) {
            std::string k = tk.peek();
            if (k != "type:" && k != "file:") break;
            tk.next();
            if (k == "type:") type = tk.nextInt();
            else              file = tk.next();
        }
        if (section == 1) {
            ix.byId[id] = (int)ix.objects.size();
            ix.objects.push_back({ id, type, file });
        } else {
            ix.backgrounds.push_back({ id, file });
        }
    }
    return ix;
}

inline Index loadIndex(const char* path) { return parseIndex(loadText(path)); }

// ── Background (bg.dat) ───────────────────────────────────────────────────────
//  Parsed straight from the original bg.dat. Layout + semantics mirror the
//  game's own loader (binary FUN_0040bff0 @ 0x40bff0) and draw routine
//  (FUN_0041a250 @ 0x41a250):
//
//    name: <string>        scene name ('_' → ' ', exactly like the loader)
//    width: <int>          total scene width in world px (camera scroll extent)
//    zboundary: <t> <b>    walkable z band (near/far foot line)
//    perspective: <a> <b>  (parsed, unused by this engine)
//    shadow: <file> shadowsize: <w> <h>
//
//    layer: … layer_end     one block per parallax layer, IN DRAW ORDER (back→front)
//      <file>               first token after "layer:" is the bitmap path
//      transparency: <0|1>  1 = colour-key (black) the layer
//      width: <int>         PARALLAX width — NOT the image width. Scroll speed is
//                           (width-REF)/(bgWidth-REF): a layer whose width==bgWidth
//                           tracks the camera 1:1 (foreground), width==REF (794)
//                           stays fixed (far background). THIS is what the old
//                           hardcoded renderer got wrong (it had no parallax).
//      x: <int>  y: <int>   world position of the (first) copy
//      height: <int>        rect layers only (image layers use natural height)
//      loop: <int>          tile spacing in world px; 0 = draw the image once
//      rect: <int>          solid-colour fill (RGB565) instead of a bitmap
//      cc/c1/c2             colour-cycling animation (parsed, unused)
struct BgLayer {
    std::string file;
    int  transparency = 0;
    int  width = 0;      // parallax width (see note above)
    int  x = 0, y = 0;
    int  height = 0;
    int  loop = 0;       // 0 = draw once, else tile every `loop` px
    long rect = -1;      // -1 = bitmap layer; >=0 = RGB565 solid-fill colour
    int  cc = 0, c1 = 0, c2 = 0;
    bool isRect() const { return rect >= 0; }
};

struct Background {
    std::string name;
    int width = 0;              // total scene width (world px)
    int zTop = 0, zBottom = 0;  // zboundary (near/far foot line)
    int perspA = 0, perspB = 0; // parsed, unused
    std::string shadow;
    int shadowW = 0, shadowH = 0;
    std::vector<BgLayer> layers;
    bool ok = false;

    // Screen-play width the original uses as the parallax denominator (0x31a).
    static constexpr int PARALLAX_REF = 794;

    // Screen-x of a layer for a given camera scroll (camX in world px). Faithful
    // to FUN_0041a250: off = (layerWidth - REF) * camX / (bgWidth - REF).
    int layerScreenX(const BgLayer& L, int camX) const {
        if (width <= PARALLAX_REF) return L.x;           // no parallax on tiny scenes
        long off = (long)(L.width - PARALLAX_REF) * camX / (long)(width - PARALLAX_REF);
        return L.x - (int)off;
    }
};

// Decode a bg.dat rect: field (RGB565) into 8-bit RGB. (Lion Forest's 4706 →
// ~16,76,16 — the dark green the old renderer had hardcoded.)
inline void rectColor(long v, int& r, int& g, int& b) {
    r = (int)((v >> 11) & 0x1f) << 3;
    g = (int)((v >> 5)  & 0x3f) << 2;
    b = (int)( v        & 0x1f) << 3;
}

inline Background parseBackground(const std::string& text) {
    using namespace detail;
    Background bg;
    Tok tk(text);
    while (!tk.eof()) {
        std::string t = tk.next();
        if (t == "name:") {
            bg.name = tk.restOfLine();
            for (char& c : bg.name) if (c == '_') c = ' ';
        }
        else if (t == "width:")       { bg.width   = tk.nextInt(); }
        else if (t == "zboundary:")   { bg.zTop    = tk.nextInt(); bg.zBottom = tk.nextInt(); }
        else if (t == "perspective:") { bg.perspA  = tk.nextInt(); bg.perspB  = tk.nextInt(); }
        else if (t == "shadow:") {
            bg.shadow = tk.next();
            std::string s = tk.next();                    // literal "shadowsize:"
            if (s == "shadowsize:") { bg.shadowW = tk.nextInt(); bg.shadowH = tk.nextInt(); }
        }
        else if (t == "layer:") {
            BgLayer L;
            L.file = tk.next();                           // path on the next line
            while (!tk.eof()) {
                std::string k = tk.next();
                if      (k == "layer_end")     break;
                else if (k == "transparency:") L.transparency = tk.nextInt();
                else if (k == "width:")        L.width  = tk.nextInt();
                else if (k == "x:")            L.x      = tk.nextInt();
                else if (k == "y:")            L.y      = tk.nextInt();
                else if (k == "height:")       L.height = tk.nextInt();
                else if (k == "loop:")         L.loop   = tk.nextInt();
                else if (k == "rect:")         L.rect   = std::atol(tk.next().c_str());
                else if (k == "rect32:")       L.rect   = std::atol(tk.next().c_str());
                else if (k == "cc:")           L.cc     = tk.nextInt();
                else if (k == "c1:")           L.c1     = tk.nextInt();
                else if (k == "c2:")           L.c2     = tk.nextInt();
                // stray tokens are skipped (matches the loader's ignore-unknown)
            }
            bg.layers.push_back(L);
        }
    }
    bg.ok = bg.width > 0 && !bg.layers.empty();
    return bg;
}

inline Background loadBackground(const char* path) { return parseBackground(loadText(path)); }

} // namespace dat
