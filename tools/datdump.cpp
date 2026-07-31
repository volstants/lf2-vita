// ─────────────────────────────────────────────────────────────────────────────
//  datdump — inspect original LF2 .dat files (host tool, not built for Vita)
//
//  Build:  g++ -std=c++17 -I src tools/datdump.cpp -o datdump
//  Usage:  ./datdump data/dennis.dat            # summary
//          ./datdump data/dennis.dat --frames   # every frame
//          ./datdump data/dennis.dat --frame 60 # one frame, full detail
//          ./datdump data/dennis.dat --plain    # dump decrypted text
// ─────────────────────────────────────────────────────────────────────────────
#include "engine/dat.hpp"
#include <cstdio>
#include <cstring>
#include <string>

static void printFrame(const dat::Frame& f) {
    std::printf("frame %3d  %-14s pic:%-4d state:%-4d wait:%-3d next:%-4d "
                "dv(%d,%d,%d) center(%d,%d)\n",
                f.id, f.name.c_str(), f.pic, f.state, f.wait, f.next,
                f.dvx, f.dvy, f.dvz, f.centerx, f.centery);
    for (const auto& b : f.bdys)
        std::printf("    bdy  kind:%d  x:%d y:%d w:%d h:%d\n", b.kind, b.x, b.y, b.w, b.h);
    for (const auto& i : f.itrs)
        std::printf("    itr  kind:%d  x:%d y:%d w:%d h:%d  dvx:%d dvy:%d "
                    "fall:%d injury:%d bdefend:%d effect:%d zwidth:%d "
                    "arest:%d vrest:%d\n",
                    i.kind, i.x, i.y, i.w, i.h, i.dvx, i.dvy,
                    i.fall, i.injury, i.bdefend, i.effect, i.zwidth,
                    i.arest, i.vrest);
    for (const auto& o : f.opoints)
        std::printf("    opoint kind:%d x:%d y:%d action:%d oid:%d facing:%d dv(%d,%d)\n",
                    o.kind, o.x, o.y, o.action, o.oid, o.facing, o.dvx, o.dvy);
    for (const auto& w : f.wpoints)
        std::printf("    wpoint kind:%d x:%d y:%d weaponact:%d attacking:%d cover:%d\n",
                    w.kind, w.x, w.y, w.weaponact, w.attacking, w.cover);
    for (const auto& c : f.cpoints)
        std::printf("    cpoint kind:%d x:%d y:%d (+%zu fields)\n",
                    c.kind, c.x, c.y, c.vals.size());
    for (const auto& b : f.bpoints)
        std::printf("    bpoint x:%d y:%d\n", b.x, b.y);
    if (!f.sound.empty()) std::printf("    sound %s\n", f.sound.c_str());
    if (f.hit_a || f.hit_j || f.hit_d || f.hit_Fa || f.hit_Ua || f.hit_Da ||
        f.hit_Fj || f.hit_Uj || f.hit_Dj || f.hit_ja)
        std::printf("    hits a:%d j:%d d:%d Fa:%d Ua:%d Da:%d Fj:%d Uj:%d Dj:%d ja:%d\n",
                    f.hit_a, f.hit_j, f.hit_d, f.hit_Fa, f.hit_Ua, f.hit_Da,
                    f.hit_Fj, f.hit_Uj, f.hit_Dj, f.hit_ja);
}

// ── Validation: cross-check parsed data against LF2 invariants ───────────────
// Returns number of problems found. Catches parser bugs that a synthetic
// sample cannot: pics outside declared sheets, dangling `next` targets, etc.
static int validate(const dat::File& d, const char* path) {
    int bad = 0, warn = 0;
    auto err = [&](const char* fmt, auto... a) {
        std::printf("  ERR  [%s] ", path); std::printf(fmt, a...); std::printf("\n"); ++bad;
    };
    auto note = [&](const char* fmt, auto... a) {
        std::printf("  warn [%s] ", path); std::printf(fmt, a...); std::printf("\n"); ++warn;
    };

    // Only character files (type 0) use hit_* as real frame jumps. For type-3
    // effect objects those fields are repurposed by the engine and routinely
    // point at frames that don't exist in the file.
    const bool isCharacter = !d.header.name.empty() && d.header.get("walking_speed") > 0.f;

    for (const auto& f : d.frames) {
        // pic must live in a declared sheet (pic 999 = blank, used by LF2)
        if (f.pic >= 0 && f.pic != 999 && !d.header.sheetOf(f.pic))
            note("frame %d (%s): pic %d outside every declared sheet",
                 f.id, f.name.c_str(), f.pic);

        // `next`/`hit_*` semantics:
        //   >0    jump to that frame id
        //   0     fall through to frame 0 (standing / first frame)
        //   999   special: remove this object from the world
        //   1000+ special engine actions (1100 = wait for weapon, etc.)
        //   <0    mirrored jump: go to frame |v| and flip facing
        auto chk = [&](const char* n, int v) {
            int id = v < 0 ? -v : v;       // negative = jump + flip facing
            if (id == 0 || id == 999 || id >= 1000 || dat::isUnset(v)) return;
            if (!d.frame(id)) err("frame %d: %s -> %d does not exist", f.id, n, v);
        };
        chk("next", f.next);
        if (isCharacter) {
            chk("hit_a", f.hit_a);   chk("hit_d", f.hit_d);   chk("hit_j", f.hit_j);
            chk("hit_Fa", f.hit_Fa); chk("hit_Ua", f.hit_Ua); chk("hit_Da", f.hit_Da);
            chk("hit_Fj", f.hit_Fj); chk("hit_Uj", f.hit_Uj); chk("hit_Dj", f.hit_Dj);
            chk("hit_ja", f.hit_ja);
        }

        for (const auto& b : f.bdys)
            if (b.w <= 0 || b.h <= 0) err("frame %d: degenerate bdy %dx%d", f.id, b.w, b.h);
        for (const auto& i : f.itrs)
            if (i.w < 0 || i.h < 0)   err("frame %d: negative itr %dx%d", f.id, i.w, i.h);
    }

    for (const auto& s : d.header.files) {
        // row*col is often inconsistent with the declared pic range in the
        // original files (bat_ball declares 12 pics with row:4 col:2). The real
        // engine only ever uses `row` to wrap, so this is informational.
        int cap = s.row * s.col, declared = s.endPic - s.startPic + 1;
        if (cap != declared)
            note("sheet %s: row*col=%d but range declares %d pics (col is unused)",
                 s.path.c_str(), cap, declared);
        if (s.w <= 0 || s.h <= 0 || s.row <= 0)
            err("sheet %s: bad geometry %dx%d row:%d", s.path.c_str(), s.w, s.h, s.row);
    }
    if (warn) std::printf("  (%d warning%s)\n", warn, warn == 1 ? "" : "s");
    return bad;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: datdump <file.dat> [--frames|--frame N|--plain|--validate]\n"
            "       datdump <data.txt> --index\n");
        return 1;
    }
    std::string text = dat::loadText(argv[1]);
    if (text.empty()) {
        std::fprintf(stderr, "error: cannot read or decrypt '%s'\n", argv[1]);
        return 2;
    }

    if (argc > 2 && std::strcmp(argv[2], "--plain") == 0) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }

    if (argc > 2 && std::strcmp(argv[2], "--index") == 0) {
        dat::Index ix = dat::parseIndex(text);
        std::printf("objects: %zu   backgrounds: %zu\n",
                    ix.objects.size(), ix.backgrounds.size());
        for (const auto& o : ix.objects)
            std::printf("  id %-5d type %d  %s\n", o.id, o.type, o.file.c_str());
        for (const auto& b : ix.backgrounds)
            std::printf("  bg %-5d %s\n", b.id, b.file.c_str());
        return 0;
    }

    dat::File d = dat::parse(text);

    if (argc > 2 && std::strcmp(argv[2], "--validate") == 0) {
        int bad = validate(d, argv[1]);
        std::printf("%-24s frames:%-4zu problems:%d\n",
                    argv[1], d.frames.size(), bad);
        return bad ? 4 : 0;
    }

    std::printf("== %s ==\n", argv[1]);
    std::printf("name   : %s\n", d.header.name.c_str());
    std::printf("head   : %s\n", d.header.head.c_str());
    std::printf("small  : %s\n", d.header.small.c_str());
    std::printf("sheets : %zu\n", d.header.files.size());
    for (const auto& s : d.header.files)
        std::printf("   pics %3d-%-3d  %-32s %dx%d  row:%d col:%d\n",
                    s.startPic, s.endPic, s.path.c_str(), s.w, s.h, s.row, s.col);
    std::printf("stats  :");
    for (const auto& kv : d.header.vals) std::printf(" %s=%g", kv.first.c_str(), kv.second);
    std::printf("\nframes : %zu\n", d.frames.size());

    if (argc > 2 && std::strcmp(argv[2], "--frames") == 0) {
        for (const auto& f : d.frames) printFrame(f);
    } else if (argc > 3 && std::strcmp(argv[2], "--frame") == 0) {
        const dat::Frame* f = d.frame(std::atoi(argv[3]));
        if (!f) { std::fprintf(stderr, "frame %s not found\n", argv[3]); return 3; }
        printFrame(*f);
    } else {
        // Summary: count of frames carrying attack boxes
        size_t withItr = 0, withOpoint = 0;
        for (const auto& f : d.frames) {
            if (!f.itrs.empty())    ++withItr;
            if (!f.opoints.empty()) ++withOpoint;
        }
        std::printf("  frames with itr   : %zu\n", withItr);
        std::printf("  frames with opoint: %zu\n", withOpoint);
    }
    return 0;
}
