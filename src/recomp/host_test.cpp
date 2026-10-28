// host_test.cpp — headless host for the recompiled lf2.exe, used to prove the
// translation against the Unicorn oracle (tools/emu/oracle.py).
//
//   lf2-recomp-test <game_dir> <frames> [events.txt] [out_dir] [dump_frames]
//
// Virtual time and message-driven input exactly as the oracle. For every frame
// it prints "frame <n> <hash>" over .data + the used guest heap; dump_frames
// (comma list) also writes the raw memory and the screen as PPM.
#include "recomp_rt.h"
#include "guest_layout.h"
#include "rt_host.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
namespace win32 { extern bool g_noBlit; extern uint64_t g_blitPixels, g_blitCalls; }

static std::string g_root, g_out;
static long g_frames = 0, g_limit = 60;
static std::multimap<long, std::array<uint32_t, 3>> g_events;
static std::set<long> g_dumpFrames;
static std::vector<uint32_t> g_screen;
static int g_sw = 0, g_sh = 0;
static FILE* g_soundLog = nullptr;
static bool g_pipe = false;          // LF2_PIPE=1: a controller on stdin/stdout drives input
static FILE* g_pipeLog = nullptr;    // events the controller sent, replayable on the oracle

namespace host {
std::string gameRoot() { return g_root; }

// case-insensitive path resolution, like the Windows filesystem
static bool resolve(const std::string& rel, std::string& out) {
    std::string cur = g_root;
    std::string part;
    std::stringstream ss(rel);
    std::vector<std::string> parts;
    std::string p = rel;
    for (char& c : p) if (c == '\\') c = '/';
    size_t start = 0;
    while (start <= p.size()) {
        size_t e = p.find('/', start);
        if (e == std::string::npos) e = p.size();
        std::string seg = p.substr(start, e - start);
        if (!seg.empty()) parts.push_back(seg);
        start = e + 1;
    }
    for (const std::string& seg : parts) {
        DIR* d = opendir(cur.c_str());
        if (!d) return false;
        std::string found;
        while (dirent* de = readdir(d))
            if (strcasecmp(de->d_name, seg.c_str()) == 0) { found = de->d_name; break; }
        closedir(d);
        if (found.empty()) return false;
        cur += "/" + found;
    }
    out = cur;
    return true;
}

bool readFile(const std::string& rel, std::vector<uint8_t>& out) {
    std::string path;
    if (!resolve(rel, path)) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}
// files the game writes are dropped, unless LF2_WRITE_DIR names a directory
// to keep them in (flat names: separators become '_')
void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    const char* dir = getenv("LF2_WRITE_DIR");
    if (!dir) return;
    std::string name = path;
    for (char& c : name) if (c == '/' || c == '\\' || c == ':') c = '_';
    if (FILE* f = fopen((std::string(dir) + "/" + name).c_str(), "wb")) {
        if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
        fclose(f);
    }
}
void log(const char* tag, const char* msg) {
    if (getenv("LF2_VERBOSE")) fprintf(stderr, "[%s] %s\n", tag, msg);
}
void fatal(const char* msg) { fprintf(stderr, "FATAL: %s\n", msg); exit(2); }

static uint32_t crc32(const uint8_t* p, size_t n) {   // zlib.crc32 (slicing-by-8)
    static uint32_t t[8][256];
    if (!t[0][1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i)
            for (int s = 1; s < 8; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xff];
    }
    uint32_t c = 0xFFFFFFFFu;
    while (n >= 8) {
        uint32_t lo, hi;
        memcpy(&lo, p, 4); memcpy(&hi, p + 4, 4);
        lo ^= c;
        c = t[7][lo & 0xff] ^ t[6][(lo >> 8) & 0xff] ^ t[5][(lo >> 16) & 0xff] ^ t[4][lo >> 24] ^
            t[3][hi & 0xff] ^ t[2][(hi >> 8) & 0xff] ^ t[1][(hi >> 16) & 0xff] ^ t[0][hi >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = t[0][(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void writePpm(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_sw, g_sh);
    for (uint32_t v : g_screen) { uint8_t c[3] = {(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(c, 1, 3, f); }
    fclose(f);
}

// Controller protocol (tools/scen/driver.py), once per frame:
//   host -> "F <frame>"
//   ctrl -> "K msg wp lp" (hex; posted now and logged as an events line)
//           "R addr len"  (hex; host answers one hex line)
//           "S path"      (screen as PPM)      "N" next frame      "Q" quit
static void pipeFrame() {
    printf("F %ld\n", g_frames);
    fflush(stdout);
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char c = line[0];
        if (c == 'N') return;
        if (c == 'Q') { fflush(stdout); if (g_pipeLog) fclose(g_pipeLog); exit(0); }
        if (c == 'K') {
            unsigned m, w, l;
            if (sscanf(line + 1, "%x %x %x", &m, &w, &l) == 3) {
                win32::postMessage(m, w, l);
                if (g_pipeLog) fprintf(g_pipeLog, "%ld %x %x %x\n", g_frames, m, w, l);
            }
        } else if (c == 'R') {
            unsigned a, n;
            if (sscanf(line + 1, "%x %x", &a, &n) == 2 && (uint64_t)a + n <= GUEST_END) {
                static const char hx[] = "0123456789abcdef";
                std::string o(2 * n, '0');
                for (unsigned i = 0; i < n; ++i) { o[2 * i] = hx[M[a + i] >> 4]; o[2 * i + 1] = hx[M[a + i] & 15]; }
                printf("%s\n", o.c_str());
            } else printf("\n");
            fflush(stdout);
        } else if (c == 'S') {
            std::string p(line + 2);
            while (!p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
            writePpm(p);
        }
    }
    exit(0);
}

void beginFrame() {
    if (g_frames > 0 && !getenv("LF2_NOHASH")) {
        // LF2_HASH_EVERY=N: heap CRC every N frames (oracle.py --hash-every)
        static long every = getenv("LF2_HASH_EVERY") ? atol(getenv("LF2_HASH_EVERY")) : 1;
        uint32_t d = crc32(M + 0x44D000, 0x459524 - 0x44D000);
        if (every <= 1 || g_frames % every == 0)
            printf("frame %ld data %08x heap %08x top %08x\n", g_frames, d,
                   crc32(M + GUEST_HEAP_BASE, rt::g_heap - GUEST_HEAP_BASE), rt::g_heap);
        else
            printf("frame %ld data %08x top %08x\n", g_frames, d, rt::g_heap);
    }
    // LF2_SCREEN_HASH=1: CRC of the presented screen every frame (to compare
    // two builds of the program, e.g. decompiled functions on and off)
    static bool screenHash = getenv("LF2_SCREEN_HASH") != nullptr;
    if (screenHash && g_frames > 0 && !g_screen.empty())
        printf("screen %ld %08x\n", g_frames, crc32((const uint8_t*)g_screen.data(), g_screen.size() * 4));
    if (g_frames > 0) {
        if (g_dumpFrames.count(g_frames) && !g_out.empty()) {
            char b[64];
            snprintf(b, sizeof b, "/recomp_mem_%04ld.bin", g_frames);
            FILE* f = fopen((g_out + b).c_str(), "wb");
            fwrite(M + 0x44D000, 1, 0x459524 - 0x44D000, f);
            fwrite(M + GUEST_HEAP_BASE, 1, rt::g_heap - GUEST_HEAP_BASE, f);
            fclose(f);
            snprintf(b, sizeof b, "/recomp_%04ld.ppm", g_frames);
            writePpm(g_out + b);
        }
    }
    if (getenv("LF2_BLITSTATS") && g_frames % 100 == 0) {
        fprintf(stderr, "frame %ld blit px/frame %llu calls/frame %llu\n", g_frames,
                (unsigned long long)(win32::g_blitPixels / 100), (unsigned long long)(win32::g_blitCalls / 100));
        win32::g_blitPixels = win32::g_blitCalls = 0;
    }
    if (g_frames >= g_limit) {
        printf("end frame %ld heap_top %08x\n", g_frames, rt::g_heap);
        fflush(stdout); exit(0);
    }
    ++g_frames;
    if (getenv("LF2_TRACE_FRAME")) rt::g_traceStubs = g_frames == atol(getenv("LF2_TRACE_FRAME"));
    if (getenv("LF2_ITRACE_FRAME")) {
        rc_tracing = g_frames == atol(getenv("LF2_ITRACE_FRAME"));
        rc_trace_flush();
#ifdef RC_WATCH
        if (rc_tracing && getenv("LF2_WATCH")) {   // "addr,len" (hex)
            unsigned a = 0, n = 0;
            sscanf(getenv("LF2_WATCH"), "%x,%x", &a, &n);
            rc_watch_lo = a; rc_watch_hi = a + n;
        } else rc_watch_lo = rc_watch_hi = 0;
#endif
    }
    win32::g_frame = (uint32_t)g_frames;
    auto r = g_events.equal_range(g_frames);
    for (auto it = r.first; it != r.second; ++it)
        win32::postMessage(it->second[0], it->second[1], it->second[2]);
    if (g_pipe) pipeFrame();
}
static long g_presents = 0;
void present(const uint32_t* xrgb, int w, int h) {
    ++g_presents;
    if (getenv("LF2_COUNT_PRESENTS") && g_frames <= 1) fprintf(stderr, "present %ld at frame %ld\n", g_presents, g_frames);
    g_screen.assign(xrgb, xrgb + (size_t)w * h);
    g_sw = w; g_sh = h;
}
uint32_t ticks() { return 0; }
void sleep(uint32_t) {}
void pumpEvents() {}
void soundPlay(int id, const uint8_t*, uint32_t bytes, int ch, int rate, int bits, int vol, int pan) {
    if (g_soundLog) fprintf(g_soundLog, "%ld play %d %u %d %d %d %d %d\n", g_frames, id, bytes, ch, rate, bits, vol, pan);
}
void soundPrepare(const uint8_t*, uint32_t, int, int, int) {}
void soundStop(int id) { if (g_soundLog) fprintf(g_soundLog, "%ld stop %d\n", g_frames, id); }
void musicPlay(const std::string& f) { if (g_soundLog) fprintf(g_soundLog, "%ld music %s\n", g_frames, f.c_str()); }
void musicStop() { if (g_soundLog) fprintf(g_soundLog, "%ld musicstop\n", g_frames); }
void musicVolume(int v) { if (g_soundLog) fprintf(g_soundLog, "%ld musicvol %d\n", g_frames, v); }
void quit() { fflush(stdout); exit(0); }
}  // namespace host

extern "C" void f_0043e9a0(void);
extern "C" { int g_host_hold_frames = 0; }
extern "C" void w_0043e9a0(void) {
    if (!g_host_hold_frames) host::beginFrame();
    f_0043e9a0();
}

int main(int argc, char** argv) {
    win32::g_noBlit = getenv("LF2_NOBLIT") != nullptr;
    // LF2_VITA_UI=1: the Vita adaptations (no ads, no mouse cursor)
    win32::g_noAds = win32::g_hideCursor = getenv("LF2_VITA_UI") != nullptr;
    if (argc < 3) { fprintf(stderr, "usage: %s game_dir frames [events] [out] [dumps]\n", argv[0]); return 1; }
    g_root = argv[1];
    g_limit = atol(argv[2]);
    if (argc > 3 && strcmp(argv[3], "-")) {
        std::ifstream in(argv[3]);
        long f; uint32_t m, w, l;
        while (in >> f >> std::hex >> m >> w >> l >> std::dec) g_events.insert({f, {m, w, l}});
    }
    g_pipe = getenv("LF2_PIPE") != nullptr;
    if (g_pipe && getenv("LF2_PIPE_LOG")) g_pipeLog = fopen(getenv("LF2_PIPE_LOG"), "w");
    if (argc > 4) { g_out = argv[4]; g_soundLog = fopen((g_out + "/recomp_sound.log").c_str(), "w"); }
    if (argc > 5) {
        std::stringstream ss(argv[5]);
        std::string t;
        while (std::getline(ss, t, ',')) g_dumpFrames.insert(atol(t.c_str()));
    }
    std::vector<uint8_t> exe;
    // LF2_EXE_PATH=<file>: take the executable from outside the game folder
    // (proves nothing in the game reads lf2.exe as a file, as on the Vita)
    if (const char* ep = getenv("LF2_EXE_PATH")) {
        std::ifstream in(ep, std::ios::binary);
        exe.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (exe.empty()) { fprintf(stderr, "cannot read %s\n", ep); return 1; }
    } else if (!host::readFile("lf2.exe", exe)) { fprintf(stderr, "lf2.exe not found\n"); return 1; }
    rt::initMachine();
    std::vector<rt::ImportSlot> imports;
    if (!rt::loadImage(exe, imports)) { fprintf(stderr, "bad lf2.exe\n"); return 1; }
    win32::initFiles();
    win32::bindImports(imports);
    win32::runStaticInitializers();
    win32::runWinMain();
    return 0;
}
