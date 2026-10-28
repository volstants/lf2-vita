// win32_internal.hpp — shared plumbing of the Win32 emulation layer.
// Mirrors tools/emu/runtime.py; any allocation or handle order change there
// must be made here too (the oracle diff depends on it).
#pragma once
#include "recomp_rt.h"
#include "guest_layout.h"
#include "rt_host.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace win32 {

// ── stub calling convention helpers ─────────────────────────────────────────
inline uint32_t A(int i) { return RD32(R.esp + 4 + 4 * i); }
inline int32_t Ai(int i) { return (int32_t)RD32(R.esp + 4 + 4 * i); }
inline double Af64(int i) { return RDF64(R.esp + 4 + 4 * i); }
inline uint32_t Aaddr(int i) { return R.esp + 4 + 4 * i; }
inline void ret(int nargs, uint32_t eax) { R.eax = eax; R.esp += 4 + 4 * nargs; }
inline void retCdecl(uint32_t eax) { R.eax = eax; R.esp += 4; }

std::string cstr(uint32_t a, size_t limit = 4096);
void wstr(uint32_t a, const std::string& s);

// ── handles (shared counter, as runtime.py handle()) ────────────────────────
uint32_t newHandle();

// ── bitmaps & surfaces ──────────────────────────────────────────────────────
struct Bitmap {
    int w = 0, h = 0;
    bool indexed = false;
    std::vector<uint8_t> idx;      // indexed: w*h palette indices, top-down
    uint32_t pal[256] = {};
    std::vector<uint32_t> rgb;     // !indexed: w*h XRGB
    // A "_mirror.bmp" identical to its base flipped shares the base pixels.
    std::shared_ptr<const Bitmap> flipOf;
    bool hidden = false;           // Vita: never drawn (g_hideCursor)
    int hideRect[4] = {0, 0, 0, 0}; // Vita: source area never drawn (g_noAds)
    bool hudFrame = false;         // the FRAME resource: one cell of the battle status panel
    uint32_t at(int x, int y) const {
        if (flipOf) return flipOf->at(w - 1 - x, y);
        return indexed ? pal[idx[(size_t)y * w + x]] : rgb[(size_t)y * w + x];
    }
};
std::shared_ptr<Bitmap> decodeDib(const uint8_t* d, size_t n, int offBits);
std::shared_ptr<Bitmap> decodeBmpFile(const std::vector<uint8_t>& f);

struct Surface {
    int w = 0, h = 0;
    uint32_t caps = 0;
    // Pixels are either a shared indexed bitmap (+ horizontal flip view) or
    // an owned XRGB buffer. The indexed form keeps the 472 sprite sheets at
    // 1 byte/pixel on the Vita; any write converts to XRGB.
    std::shared_ptr<Bitmap> src;
    std::vector<uint32_t> rgb;
    bool hasKey = false;
    bool hidden = false;           // pixels came from a hidden bitmap
    const int* hideRect = nullptr;  // source area not drawn (see Bitmap)
    bool hudFrame = false;          // pixels came from FRAME (see g_hudDrawn)
    uint32_t keyLo = 0, keyHi = 0;
    uint32_t lockMem = 0;
    void makeRgb();
    uint32_t px(int x, int y) const {
        if (!rgb.empty()) return rgb[(size_t)y * w + x];
        if (src) return src->at(x, y);
        return 0;
    }
};

// ── COM ─────────────────────────────────────────────────────────────────────
struct ComMethod { const char* name; int nargs; rt::StubFn fn; };
uint32_t comObject(const char* kind, void* native);
void* comNative(uint32_t obj);
void registerComKind(const char* kind, std::vector<ComMethod> methods);

void initDirectX();
void initFiles();
extern const uint32_t SCREEN_W, SCREEN_H;
extern uint32_t g_frame;
Surface* surfaceOf(uint32_t obj);

}  // namespace win32

// Declares a stub: body computes the EAX value; the macro pops stdcall args.
#define W32_STD(fname, nargs) \
    static uint32_t fname##_impl(); \
    static void fname() { uint32_t v_ = fname##_impl(); win32::ret(nargs, v_); } \
    static uint32_t fname##_impl()
#define W32_CDECL(fname) \
    static uint32_t fname##_impl(); \
    static void fname() { uint32_t v_ = fname##_impl(); win32::retCdecl(v_); } \
    static uint32_t fname##_impl()
