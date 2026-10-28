// rt_host.hpp — seams between the recompiled game, the Win32 emulation layer
// (win32_*.cpp) and the platform (host_sdl.cpp / tests).
#pragma once
#include <cstdarg>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace rt {
typedef void (*StubFn)(void);

struct ImportSlot { std::string dll, name; uint32_t slot; };

extern uint32_t g_heap;
extern bool g_traceStubs;
uint32_t newStub(const char* name, StubFn fn);
void setStub(uint32_t addr, StubFn fn);
uint32_t alloc(uint32_t n, uint32_t align = 16);
uint32_t gameAlloc(uint32_t n, bool zero);
void gameFree(uint32_t p);
[[noreturn]] void fatal(const char* fmt, ...);
bool loadImage(const std::vector<uint8_t>& exe, std::vector<ImportSlot>& imports);
void initMachine();
uint32_t callGuest(uint32_t fn, std::initializer_list<uint32_t> args, uint32_t ecx = 0);
const char* stubName(uint32_t addr);
}

namespace win32 {
// Build the import table and bind every import. Returns false on a missing one.
bool bindImports(const std::vector<rt::ImportSlot>& imports);
void runStaticInitializers();
void runWinMain();                 // never returns until the game quits
void postMessage(uint32_t msg, uint32_t wp, uint32_t lp);
extern bool g_virtualTime;          // oracle-compatible deterministic clock
extern uint32_t g_now;              // virtual timeGetTime
extern uint32_t g_frame;
void initFiles();
// Vita adaptations, off in the test host so the oracle comparison stays exact:
//   g_noAds      the ad sheets sprite\sys\ad0/ad1.bmp read as missing (the
//                original then draws no ads) and the "Update on/off" toggle
//                (ad downloads, from MENU_CLIP7) is not drawn;
//   g_hideCursor the game's own mouse cursor (LF2_CURSOR*) is not drawn.
extern bool g_noAds, g_hideCursor;
// Set whenever the game blits a cell of the battle status panel (the FRAME
// resource; 8 per battle frame, pause included, never outside a battle). The
// host reads and clears it at each present to pick the Vita HUD layout. It is
// only observed: the game's memory and drawing are unchanged.
extern bool g_hudDrawn;
struct Surface;
const Surface* primarySurface();
void surfaceToXRGB(const Surface* s, std::vector<uint32_t>& out, int& w, int& h);
}

namespace host {
// Filesystem root holding the original game files (data/, bg/, sprite/, bgm/).
std::string gameRoot();
bool readFile(const std::string& relWindowsPath, std::vector<uint8_t>& out);
// Persist a file the game wrote (settings, recordings); no-op in tests.
void writeFile(const std::string& relWindowsPath, const std::vector<uint8_t>& data);
void log(const char* tag, const char* msg);
[[noreturn]] void fatal(const char* msg);
// Called once per original frame (entry of 0x43e9a0): input pump, hooks.
void beginFrame();
// The game blitted to the primary surface: show it.
void present(const uint32_t* xrgb, int w, int h);
// Real time (only when !win32::g_virtualTime)
uint32_t ticks();
void sleep(uint32_t ms);
// Poll platform input; translate into WM_* via win32::postMessage.
void pumpEvents();
// Sound: `id` is a stable buffer handle; data is the PCM the game locked in.
void soundPlay(int id, const uint8_t* pcm, uint32_t bytes, int channels, int rate,
               int bits, int volumeHundredthsDb, int panHundredthsDb);
void soundStop(int id);
// The game finished writing a sound buffer (IDirectSoundBuffer::Unlock): the
// host may convert it ahead of its first play. No effect on the game.
void soundPrepare(const uint8_t* pcm, uint32_t bytes, int channels, int rate, int bits);
void musicPlay(const std::string& windowsPath);   // e.g. "bgm\\main.wma"
void musicStop();
void musicVolume(int hundredthsDb);
void quit();
}
