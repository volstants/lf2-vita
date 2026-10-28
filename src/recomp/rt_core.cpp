// rt_core.cpp — guest machine for the recompiled lf2.exe: memory, PE image,
// control transfer and the handful of CPU helpers the translation calls.
#include "recomp_rt.h"
#include "guest_layout.h"
#include "rt_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <map>
#include <unordered_map>
#include <vector>
#ifdef RC_IDENTITY
#include <sys/mman.h>
#endif

extern "C" {
rc_cpu R;
uint8_t* M = nullptr;
}

namespace rt {

static std::vector<StubFn> g_stubs;
static std::vector<const char*> g_stubNames;
uint32_t g_heap = GUEST_HEAP_BASE;
bool g_traceStubs = false;

uint32_t newStub(const char* name, StubFn fn) {
    g_stubs.push_back(fn);
    g_stubNames.push_back(name);
    return GUEST_STUB_BASE + 4u * (uint32_t)(g_stubs.size() - 1);
}

void setStub(uint32_t addr, StubFn fn) {
    g_stubs[(addr - GUEST_STUB_BASE) / 4] = fn;
}

uint32_t alloc(uint32_t n, uint32_t align) {
    uint32_t a = (g_heap + align - 1) & ~(align - 1);
    if (n == 0) n = 1;
    if ((uint64_t)a + n > GUEST_END) fatal("guest heap exhausted (%u bytes requested)", n);
    g_heap = a + n;
    memset(M + a, 0, n);
    return a;
}

// Game heap (malloc/new/free): exact-size LIFO free lists over the bump
// allocator — the same algorithm as runtime.py game_alloc.
static std::map<uint32_t, std::vector<uint32_t>> g_freeLists;
static std::unordered_map<uint32_t, uint32_t> g_blockSize;
uint32_t gameAlloc(uint32_t n, bool zero) {
    uint32_t size = ((n ? n : 1) + 15) & ~15u;
    uint32_t p;
    auto it = g_freeLists.find(size);
    if (it != g_freeLists.end() && !it->second.empty()) {
        p = it->second.back();
        it->second.pop_back();
        if (zero) memset(M + p, 0, size);
    } else {
        p = alloc(size);
    }
    g_blockSize[p] = size;
    return p;
}
// the free lists are host state: the differential tester saves and restores
// them with the guest memory so both sides of a trial allocate alike
static std::map<uint32_t, std::vector<uint32_t>> g_savedFreeLists;
static std::unordered_map<uint32_t, uint32_t> g_savedBlockSize;
void gameHeapSave() { g_savedFreeLists = g_freeLists; g_savedBlockSize = g_blockSize; }
void gameHeapRestore() { g_freeLists = g_savedFreeLists; g_blockSize = g_savedBlockSize; }
void gameFree(uint32_t p) {
    auto it = g_blockSize.find(p);
    if (it == g_blockSize.end()) return;
    g_freeLists[it->second].push_back(p);
    g_blockSize.erase(it);
}

void fatal(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    host::log("FATAL", buf);
    host::fatal(buf);
    abort();
}

// ── PE image ────────────────────────────────────────────────────────────────
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool loadImage(const std::vector<uint8_t>& exe, std::vector<ImportSlot>& imports) {
    if (exe.size() < 0x400 || exe[0] != 'M' || exe[1] != 'Z') return false;
    uint32_t pe = le32(&exe[0x3c]);
    const uint8_t* coff = &exe[pe + 4];
    uint16_t nsec = le16(coff + 2);
    uint16_t optsz = le16(coff + 16);
    const uint8_t* opt = coff + 20;
    uint32_t imageBase = le32(opt + 28);
    uint32_t sizeOfHeaders = le32(opt + 60);
    if (imageBase != GUEST_IMAGE_BASE) return false;
    memcpy(M + imageBase, exe.data(), sizeOfHeaders);
    const uint8_t* sec = opt + optsz;
    for (int i = 0; i < nsec; ++i, sec += 40) {
        uint32_t vsize = le32(sec + 8), va = le32(sec + 12);
        uint32_t rawsz = le32(sec + 16), rawptr = le32(sec + 20);
        uint32_t n = std::min(rawsz, std::max(vsize, rawsz));
        if (rawptr + n > exe.size()) n = (uint32_t)exe.size() - rawptr;
        memcpy(M + imageBase + va, exe.data() + rawptr, n);
    }
    // import directory
    uint32_t impRva = le32(opt + 104);
    for (uint32_t d = impRva;; d += 20) {
        const uint8_t* desc = M + imageBase + d;
        uint32_t oft = le32(desc), nameRva = le32(desc + 12), ft = le32(desc + 16);
        if (!nameRva) break;
        const char* dll = (const char*)(M + imageBase + nameRva);
        uint32_t lookup = oft ? oft : ft;
        for (uint32_t k = 0;; ++k) {
            uint32_t ent = le32(M + imageBase + lookup + 4 * k);
            if (!ent) break;
            ImportSlot s;
            s.dll = dll;
            s.slot = imageBase + ft + 4 * k;
            if (ent & 0x80000000u) {
                // pefile names WSOCK32 ordinals; do the same so both runtimes
                // bind (and name) imports identically.
                static const struct { unsigned ord; const char* name; } wsock[] = {
                    {1, "accept"}, {2, "bind"}, {3, "closesocket"}, {4, "connect"}, {9, "htons"},
                    {10, "inet_addr"}, {11, "inet_ntoa"}, {12, "ioctlsocket"}, {13, "listen"},
                    {16, "recv"}, {19, "send"}, {20, "sendto"}, {23, "socket"},
                    {51, "gethostbyaddr"}, {52, "gethostbyname"}, {57, "gethostname"},
                    {101, "WSAAsyncSelect"}, {115, "WSAStartup"}, {116, "WSACleanup"}};
                char b[16];
                snprintf(b, sizeof b, "#%u", ent & 0xffff);
                s.name = b;
                if (strcasecmp(dll, "WSOCK32.dll") == 0)
                    for (auto& w : wsock) if (w.ord == (ent & 0xffff)) s.name = w.name;
            } else {
                s.name = (const char*)(M + imageBase + ent + 2);
            }
            imports.push_back(s);
        }
    }
    return true;
}

void initMachine() {
#ifdef RC_IDENTITY
    // Decompilation harness (32-bit x86 host): guest addresses are host
    // addresses, so decompiled C can use real pointers. M stays 0.
    static bool mapped = false;
    if (!mapped) {
        void* p = mmap((void*)0x10000, GUEST_END - 0x10000, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != (void*)0x10000) fatal("cannot map guest memory at its own addresses");
        M = nullptr;
        mapped = true;
    }
#else
    if (!M) {
        M = (uint8_t*)calloc(1, GUEST_END);
        if (!M) fatal("cannot allocate %u MB of guest memory", GUEST_END >> 20);
    }
#endif
    memset(&R, 0, sizeof R);
    R.fcw = 0x027F;
    R.fs_base = GUEST_TIB_BASE;
    WR32(GUEST_TIB_BASE + 0, 0xFFFFFFFFu);
    WR32(GUEST_TIB_BASE + 4, GUEST_STACK_TOP);
    WR32(GUEST_TIB_BASE + 8, GUEST_STACK_TOP - GUEST_STACK_SIZE);
    WR32(GUEST_TIB_BASE + 0x18, GUEST_TIB_BASE);
    R.esp = GUEST_STACK_TOP - 0x1000;
    g_heap = GUEST_HEAP_BASE;
    g_freeLists.clear();
    g_blockSize.clear();
}

// Call guest code from native code: cdecl/stdcall/thiscall all fine since we
// only need the return value; the caller decides what to push.
uint32_t callGuest(uint32_t fn, std::initializer_list<uint32_t> args, uint32_t ecx) {
    uint32_t saveEsp = R.esp;
    std::vector<uint32_t> a(args);
    for (auto it = a.rbegin(); it != a.rend(); ++it) { R.esp -= 4; WR32(R.esp, *it); }
    R.esp -= 4;
    WR32(R.esp, GUEST_MAGIC_RET);
    R.ecx = ecx;
    rc_call(fn);
    uint32_t eax = R.eax;
    R.esp = saveEsp;
    return eax;
}

const char* stubName(uint32_t addr) {
    uint32_t i = (addr - GUEST_STUB_BASE) / 4;
    return i < g_stubNames.size() ? g_stubNames[i] : "?";
}

} // namespace rt

// ── translation helpers ─────────────────────────────────────────────────────
extern "C" {

void rc_call(uint32_t target) {
    if (target >= GUEST_STUB_BASE && target < GUEST_STUB_BASE + 0x100000u) {
        uint32_t i = (target - GUEST_STUB_BASE) / 4;
        if (i >= rt::g_stubs.size() || !rt::g_stubs[i])
            rt::fatal("call to unimplemented stub %s", rt::stubName(target));
        if (rt::g_traceStubs) fprintf(stderr, "stub %s\n", rt::g_stubNames[i]);
        rt::g_stubs[i]();
        return;
    }
    unsigned lo = 0, hi = rc_fn_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (rc_fn_table[mid].addr < target) lo = mid + 1; else hi = mid;
    }
    if (lo < rc_fn_count && rc_fn_table[lo].addr == target) {
        rc_fn_table[lo].fn();
        return;
    }
    rt::fatal("indirect call to unknown guest address %08x", target);
}

int rc_tracing = 0;
static uint32_t g_lastPc;
static FILE* g_traceFile;
// Binary record: pc, eax, ecx, edx, ebx, esp, ebp, esi, edi, fpu top, then the
// eight physical x87 registers as doubles (tools/emu/itrace.py writes the same).
void rc_trace(uint32_t pc, uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx,
              uint32_t esp, uint32_t ebp, uint32_t esi, uint32_t edi) {
    if (!g_traceFile) {
        const char* p = getenv("LF2_TRACE_OUT");
        g_traceFile = fopen(p ? p : "rc_trace.bin", "wb");
        if (!g_traceFile) { rc_tracing = 0; return; }
    }
    g_lastPc = pc;
    uint32_t rec[10] = {pc, eax, ecx, edx, ebx, esp, ebp, esi, edi, (uint32_t)(R.top & 7)};
    fwrite(rec, 4, 10, g_traceFile);
    fwrite(R.st, 8, 8, g_traceFile);
}
void rc_trace_flush(void) { if (g_traceFile) fflush(g_traceFile); }
uint32_t rc_last_pc(void) { return g_lastPc; }
#ifdef RC_WATCH
uint32_t rc_watch_lo = 0, rc_watch_hi = 0;
void rc_watch_hit(uint32_t a, uint32_t n, const void* v) {
    uint8_t b[8] = {0};
    memcpy(b, v, n > 8 ? 8 : n);
    fprintf(stderr, "watch pc=%08x addr=%08x n=%u val=", g_lastPc, a, n);
    for (unsigned i = 0; i < n && i < 8; ++i) fprintf(stderr, "%02x", b[i]);
    if (n == 8) { double d; memcpy(&d, b, 8); fprintf(stderr, " (%g)", d); }
    fprintf(stderr, "\n");
}
#endif

void rc_trap(uint32_t where, const char* why) {
    rt::fatal("trap at %08x: %s", where, why);
    abort();
}

void rc_cpuid(uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    uint32_t leaf = *a;
    if (leaf == 0) {           // "GenuineIntel", max leaf 1
        *a = 1; *b = 0x756e6547; *d = 0x49656e69; *c = 0x6c65746e;
    } else if (leaf == 1) {    // family 6; FPU, TSC, CMOV, MMX, SSE, SSE2
        *a = 0x000006F6; *b = 0; *c = 0;
        *d = (1u << 0) | (1u << 4) | (1u << 15) | (1u << 23) | (1u << 25) | (1u << 26);
    } else {
        *a = *b = *c = *d = 0;
    }
}

uint32_t rc_eflags(int op, uint32_t a, uint32_t b, uint32_t r, int sz, uint32_t cf, uint32_t of) {
    // PUSHFD must retain AC as well as ID: zlib 0x443ac1 toggles AC to detect 486+.
    uint32_t f = 0x202 | (R.eflags_extra & 0x00240000u);
    if (rc_cf(op, a, b, r, sz, cf)) f |= 1;
    if (rc_pf(r)) f |= 4;
    if (rc_zf(op, r, sz)) f |= 0x40;
    if (rc_sf(r, sz)) f |= 0x80;
    if (rc_of(op, a, b, r, sz, of)) f |= 0x800;
    return f;
}

void rc_stos32(uint32_t dst, uint32_t v, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) WR32(dst + 4 * i, v);
}

int32_t rc_fist32(double v) {
    double r = nearbyint(v);
    if (!(r >= -2147483648.0 && r <= 2147483647.0)) return (int32_t)0x80000000u;
    return (int32_t)r;
}

int64_t rc_fist64(double v) {
    double r = nearbyint(v);
    if (!(r >= -9223372036854775808.0 && r < 9223372036854775808.0))
        return (int64_t)0x8000000000000000ull;
    return (int64_t)r;
}

// __ftol2_sse (0x4450d0): cvttsd2si of ST(0), popped; EDX untouched.
void n_ftol2_sse(void) {
    double v = ST(0);
    FPOP();
    int32_t i;
    if (!(v > -2147483649.0 && v < 2147483648.0)) i = (int32_t)0x80000000u;
    else i = (int32_t)v;
    R.eax = (uint32_t)i;
    R.esp += 4;
}

} // extern "C"

extern "C" void rt_game_heap_save(void) { rt::gameHeapSave(); }
extern "C" void rt_game_heap_restore(void) { rt::gameHeapRestore(); }
