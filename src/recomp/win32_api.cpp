// win32_api.cpp — KERNEL32/USER32/GDI32/WINMM/WSOCK32/MSVCR80 for the
// recompiled lf2.exe. Mirrors tools/emu/runtime.py (the oracle) call by call:
// same return values, same guest allocations, same handle numbering.
#include "win32_internal.hpp"

#include <algorithm>
#include <strings.h>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace win32 {

bool g_virtualTime = true;
uint32_t g_now = 100000;
uint32_t g_frame = 0;
static uint32_t g_nextHandle = 0x1000;
static uint32_t g_randState = 1;
static uint32_t g_wndproc = 0;
static uint32_t g_tm = 0, g_fmode = 0;
static std::deque<std::array<uint32_t, 3>> g_msgq;

uint32_t newHandle() { uint32_t h = g_nextHandle; g_nextHandle += 4; return h; }

std::string cstr(uint32_t a, size_t limit) {
    std::string s;
    if (!a) return s;
    while (s.size() < limit) {
        char c = (char)M[a + s.size()];
        if (!c) break;
        s.push_back(c);
    }
    return s;
}
void wstr(uint32_t a, const std::string& s) {
    memcpy(M + a, s.data(), s.size());
    M[a + s.size()] = 0;
}

void postMessage(uint32_t msg, uint32_t wp, uint32_t lp) { g_msgq.push_back({msg, wp, lp}); }

// ── files ───────────────────────────────────────────────────────────────────
struct CFile {
    std::string path;
    bool writing = false;
    bool text = true;
    std::vector<uint8_t> data;
    size_t pos = 0;
    bool eof = false;
};
static std::unordered_map<uint32_t, CFile> g_files;
static std::map<std::string, std::vector<uint8_t>> g_vfs;

// The game builds absolute paths from _getcwd() ("C:\\LF2", see c_getcwd):
// strip that prefix so they name the same files as the relative ones.
static std::string gameRel(std::string p) {
    static const char kCwd[] = "c:\\lf2\\";
    if (p.size() > sizeof kCwd - 1 && strncasecmp(p.c_str(), kCwd, sizeof kCwd - 1) == 0)
        p = p.substr(sizeof kCwd - 1);
    return p;
}
static std::string vfsKey(std::string p) {
    p = gameRel(p);
    for (char& c : p) { if (c == '/') c = '\\'; c = (char)tolower((unsigned char)c); }
    return p;
}
static bool readGameFile(const std::string& p, std::vector<uint8_t>& out) {
    auto it = g_vfs.find(vfsKey(p));
    if (it != g_vfs.end()) { out = it->second; return true; }
    return host::readFile(gameRel(p), out);
}
void initFiles() { g_files.clear(); g_vfs.clear(); }

// Format strings live in .rdata; parse each guest string once.
static const std::string& fmtString(uint32_t a) {
    static std::unordered_map<uint32_t, std::string> cache;
    static uint32_t lastA = 0;
    static const std::string* last = nullptr;
    if (a == lastA && last) return *last;
    if (a >= 0x447000 && a < 0x44D000) {
        auto it = cache.find(a);
        if (it == cache.end()) it = cache.emplace(a, cstr(a)).first;
        lastA = a; last = &it->second;
        return it->second;
    }
    if (false) {
        auto it = cache.find(a);
        if (it != cache.end()) return it->second;
        return cache[a] = cstr(a);
    }
    static std::string tmp;
    tmp = cstr(a);
    return tmp;
}

// ── printf / scanf (runtime.py c_format / c_scan) ───────────────────────────
static std::string cFormat(const std::string& fmt, int k) {
    std::string out;
    size_t i = 0;
    while (i < fmt.size()) {
        char c = fmt[i];
        if (c != '%') { out.push_back(c); ++i; continue; }
        size_t start = i++;
        std::string flags;
        while (i < fmt.size() && strchr("-+ #0", fmt[i])) flags.push_back(fmt[i++]);
        std::string width, prec;
        if (i < fmt.size() && fmt[i] == '*') { width = std::to_string(Ai(k++)); ++i; }
        else while (i < fmt.size() && isdigit((unsigned char)fmt[i])) width.push_back(fmt[i++]);
        bool hasPrec = false;
        if (i < fmt.size() && fmt[i] == '.') {
            hasPrec = true; ++i;
            if (i < fmt.size() && fmt[i] == '*') { prec = std::to_string(Ai(k++)); ++i; }
            else while (i < fmt.size() && isdigit((unsigned char)fmt[i])) prec.push_back(fmt[i++]);
        }
        std::string len;
        if (fmt.compare(i, 3, "I64") == 0) { len = "I64"; i += 3; }
        else if (fmt.compare(i, 3, "I32") == 0) { i += 3; }
        else if (fmt.compare(i, 2, "ll") == 0) { len = "ll"; i += 2; }
        else if (fmt.compare(i, 2, "hh") == 0) { i += 2; }
        else if (i < fmt.size() && strchr("hlLI", fmt[i])) { ++i; }
        if (i >= fmt.size()) { out += fmt.substr(start); break; }
        char conv = fmt[i++];
        std::string spec = "%" + flags + width + (hasPrec ? "." + prec : "");
        char buf[512];
        switch (conv) {
        case '%': out.push_back('%'); break;
        case 'd': case 'i':
            if (len == "I64" || len == "ll") {
                int64_t v = (int64_t)((uint64_t)A(k) | ((uint64_t)A(k + 1) << 32)); k += 2;
                snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)v);
            } else snprintf(buf, sizeof buf, (spec + "d").c_str(), Ai(k++));
            out += buf; break;
        case 'u': case 'o': case 'x': case 'X':
            if (len == "I64" || len == "ll") {
                uint64_t v = (uint64_t)A(k) | ((uint64_t)A(k + 1) << 32); k += 2;
                snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), (unsigned long long)v);
            } else snprintf(buf, sizeof buf, (spec + conv).c_str(), A(k++));
            out += buf; break;
        case 'e': case 'E': case 'f': case 'g': case 'G': {
            double v = Af64(k); k += 2;
            snprintf(buf, sizeof buf, (spec + conv).c_str(), v);
            out += buf; break;
        }
        case 'c': snprintf(buf, sizeof buf, (spec + "c").c_str(), (int)(A(k++) & 0xff)); out += buf; break;
        case 's': {
            uint32_t p = A(k++);
            std::string s = p ? cstr(p, 1 << 20) : std::string("(null)");
            std::string f = spec + "s";
            int n = snprintf(nullptr, 0, f.c_str(), s.c_str());
            std::string t(n + 1, '\0');
            snprintf(&t[0], t.size(), f.c_str(), s.c_str());
            t.resize(n);
            out += t; break;
        }
        case 'p': snprintf(buf, sizeof buf, "%08X", A(k++)); out += buf; break;
        default: rt::fatal("printf conversion %%%c", conv);
        }
    }
    return out;
}

static bool isWs(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

struct ScanSrc {
    const uint8_t* d; size_t n; size_t pos;
    int peek() const { return pos < n ? d[pos] : -1; }
    int get() { return pos < n ? d[pos++] : -1; }
};

// Returns assigned count (-1 on input failure before any conversion); sets eof.
static int cScan(ScanSrc& src, const std::string& fmt, int k, bool& eof) {
    int assigned = 0;
    eof = false;
    bool started = false;
    size_t i = 0, n = fmt.size();
    while (i < n) {
        char c = fmt[i];
        if (isWs((unsigned char)c)) {
            while (src.peek() >= 0 && isWs(src.peek())) src.get();
            ++i; continue;
        }
        if (c != '%') {
            int ch = src.peek();
            if (ch < 0) { eof = true; break; }
            if (ch != (unsigned char)c) break;
            src.get(); ++i; continue;
        }
        ++i;
        bool suppress = false;
        if (i < n && fmt[i] == '*') { suppress = true; ++i; }
        size_t width = 0;
        while (i < n && isdigit((unsigned char)fmt[i])) width = width * 10 + (fmt[i++] - '0');
        std::string length;
        while (i < n && strchr("hlLI", fmt[i])) length.push_back(fmt[i++]);
        char conv = fmt[i++];
        if (conv != 'c' && conv != '[' && conv != 'n')
            while (src.peek() >= 0 && isWs(src.peek())) src.get();
        if (conv == 'n') { if (!suppress) WR32(A(k++), (uint32_t)src.pos); continue; }
        if (src.peek() < 0) { eof = true; break; }
        started = true;
        if (conv == 's') {
            std::string buf;
            while (src.peek() >= 0 && !isWs(src.peek()) && (!width || buf.size() < width))
                buf.push_back((char)src.get());
            if (src.peek() < 0) eof = true;
            if (!suppress) { wstr(A(k++), buf); ++assigned; }
        } else if (conv == 'c') {
            size_t w = width ? width : 1;
            std::string buf;
            while (buf.size() < w && src.peek() >= 0) buf.push_back((char)src.get());
            if (!suppress) { memcpy(M + A(k++), buf.data(), buf.size()); ++assigned; }
        } else if (strchr("dixuo", conv)) {
            std::string buf;
            const char* allowed = (conv == 'x' || conv == 'i') ? "0123456789abcdefABCDEFxX" : "0123456789";
            if (src.peek() == '+' || src.peek() == '-') buf.push_back((char)src.get());
            while (src.peek() >= 0 && strchr(allowed, src.peek()) && (!width || buf.size() < width))
                buf.push_back((char)src.get());
            if (src.peek() < 0) eof = true;
            // Python int() semantics: must be a complete number
            const char* s = buf.c_str();
            char* end = nullptr;
            long long v;
            if (conv == 'x') v = strtoll(s, &end, 16);
            else if (conv == 'i') v = strtoll(s, &end, 0);
            else if (conv == 'o') v = strtoll(s, &end, 8);
            else v = strtoll(s, &end, 10);
            bool digits = false;
            for (const char* p = s; *p; ++p) if (isxdigit((unsigned char)*p)) digits = true;
            if (buf.empty() || !digits || (end && *end)) break;
            if (!suppress) {
                if (length == "h") WR16(A(k++), (uint32_t)v); else WR32(A(k++), (uint32_t)v);
                ++assigned;
            }
        } else if (strchr("feEgG", conv)) {
            std::string buf;
            while (src.peek() >= 0 && strchr("0123456789+-.eE", src.peek()) && (!width || buf.size() < width)) {
                int ch = src.get();
                if ((ch == '+' || ch == '-') && !buf.empty() && buf.back() != 'e' && buf.back() != 'E') {
                    src.pos--; break;
                }
                buf.push_back((char)ch);
            }
            if (src.peek() < 0) eof = true;
            char* end = nullptr;
            double v = strtod(buf.c_str(), &end);
            if (buf.empty() || (end && *end)) break;
            if (!suppress) {
                if (length == "l" || length == "L") WRF64(A(k++), v); else WRF32(A(k++), (float)v);
                ++assigned;
            }
        } else rt::fatal("scanf conversion %%%c", conv);
    }
    if (!started && eof) return -1;
    return assigned;
}

// ── KERNEL32 ────────────────────────────────────────────────────────────────
W32_STD(k_InterlockedExchange, 2) { uint32_t o = RD32(A(0)); WR32(A(0), A(1)); return o; }
W32_STD(k_InterlockedCompareExchange, 3) { uint32_t o = RD32(A(0)); if (o == A(2)) WR32(A(0), A(1)); return o; }
W32_STD(k_GetVersionExA, 1) { return 1; }
W32_STD(k_GetThreadLocale, 0) { return 0x409; }
W32_STD(k_GetLocaleInfoA, 4) { return 0; }
W32_STD(k_GetACP, 0) { return 1252; }
W32_STD(k_GetSystemTimeAsFileTime, 1) { WR64(A(0), 0); return 0; }
W32_STD(k_GetCurrentProcessId, 0) { return 4; }
W32_STD(k_GetCurrentThreadId, 0) { return 8; }
static uint32_t nowMs() { return g_virtualTime ? g_now : host::ticks(); }
W32_STD(k_GetTickCount, 0) { return nowMs(); }
W32_STD(k_QueryPerformanceCounter, 1) { WR64(A(0), (uint64_t)nowMs() * 1000); return 1; }
W32_STD(k_IsDebuggerPresent, 0) { return 0; }
W32_STD(k_SetUnhandledExceptionFilter, 1) { return 0; }
W32_STD(k_UnhandledExceptionFilter, 1) { return 0; }
W32_STD(k_GetCurrentProcess, 0) { return 0xFFFFFFFFu; }
static void k_TerminateProcess() { host::quit(); }
W32_STD(k_GetStartupInfoA, 1) { WR32(A(0), 68); memset(M + A(0) + 4, 0, 64); return 0; }
W32_STD(k_OutputDebugStringA, 1) { host::log("dbg", cstr(A(0)).c_str()); return 0; }
W32_STD(k_lstrlenA, 1) { return (uint32_t)cstr(A(0)).size(); }
W32_STD(k_GetModuleHandleA, 1) { return A(0) == 0 ? 0x400000u : 0; }
W32_STD(k_CloseHandle, 1) { return 1; }
W32_STD(k_MultiByteToWideChar, 6) { return 0; }
W32_STD(k_CreateFileA, 7) { return 0xFFFFFFFFu; }
W32_STD(k_Sleep, 1) {
    if (g_virtualTime) g_now += A(0); else host::sleep(A(0));
    return 0;
}
W32_STD(k_LeaveCriticalSection, 1) { return 0; }
W32_STD(k_EnterCriticalSection, 1) { return 0; }
W32_STD(k_InitializeCriticalSection, 1) { return 0; }
W32_STD(k_GetLocalTime, 1) {
    const uint16_t st[8] = {2009, 7, 6, 11, 12, 0, 0, 0};
    memcpy(M + A(0), st, 16);
    return 0;
}
W32_STD(k_GetLastError, 0) { return 0; }
W32_STD(k_WriteFile, 5) { return 1; }
W32_STD(k_CreateThread, 6) { return newHandle(); }

// ── USER32 ──────────────────────────────────────────────────────────────────
W32_STD(u_TranslateMessage, 1) { return 0; }
static void fillMsg(uint32_t p, const std::array<uint32_t, 3>& m) {
    uint32_t v[7] = {0x5000, m[0], m[1], m[2], nowMs(), 0, 0};
    memcpy(M + p, v, 28);
}
W32_STD(u_PeekMessageA, 5) {
    if (!g_virtualTime && g_msgq.empty()) host::pumpEvents();
    if (!g_msgq.empty()) {
        if (A(0)) fillMsg(A(0), g_msgq.front());
        if (A(4) & 1) g_msgq.pop_front();
        return 1;
    }
    if (g_virtualTime) g_now += 1;
    return 0;
}
W32_STD(u_GetMessageA, 4) {
    if (g_msgq.empty()) { host::quit(); return 0; }
    auto m = g_msgq.front();
    g_msgq.pop_front();
    fillMsg(A(0), m);
    return m[0] == 0x12 ? 0 : 1;
}
// DispatchMessageA: enter the WndProc as if called from here. Same stack
// image as runtime.py api_DispatchMessageA.
static void u_DispatchMessageA() {
    uint32_t msgp = A(0);
    uint32_t hwnd = RD32(msgp), msg = RD32(msgp + 4), wp = RD32(msgp + 8), lp = RD32(msgp + 12);
    uint32_t ret = RD32(R.esp);
    R.esp += 8;
    R.esp -= 4; WR32(R.esp, lp);
    R.esp -= 4; WR32(R.esp, wp);
    R.esp -= 4; WR32(R.esp, msg);
    R.esp -= 4; WR32(R.esp, hwnd);
    R.esp -= 4; WR32(R.esp, ret);
    rc_call(g_wndproc);
}
W32_STD(u_MessageBoxA, 4) {
    std::string t = cstr(A(1)) + " | " + cstr(A(2));
    host::log("msgbox", t.c_str());
    return 1;
}
W32_STD(u_DestroyWindow, 1) { return 1; }
W32_STD(u_UpdateWindow, 1) { return 1; }
W32_STD(u_CreateWindowExA, 12) { return 0x5000; }
W32_STD(u_RegisterClassA, 1) { g_wndproc = RD32(A(0) + 4); return 1; }
W32_STD(u_LoadCursorA, 2) { return 0x6000; }
W32_STD(u_LoadIconA, 2) { return 0x6004; }
W32_STD(u_GetSystemMetrics, 1) { return A(0) == 0 ? 1024 : A(0) == 1 ? 768 : 0; }
W32_STD(u_GetKeyState, 1) { return 0; }
W32_STD(u_PostMessageA, 4) { return 1; }
W32_STD(u_PostQuitMessage, 1) { return 0; }
W32_STD(u_DefWindowProcA, 4) { return 0; }
W32_STD(u_SetCursor, 1) { return 0; }
W32_STD(u_ClientToScreen, 2) { return 1; }
W32_STD(u_GetClientRect, 2) {
    int32_t r[4] = {0, 0, (int32_t)SCREEN_W, (int32_t)SCREEN_H};
    memcpy(M + A(1), r, 16);
    return 1;
}
W32_STD(u_ShowWindow, 2) {
    // Windows sends WM_MOVE on the first show (runtime.py api_ShowWindow).
    static bool shown = false;
    if (!shown) { shown = true; postMessage(0x3, 0, 0); }
    return 1;
}
W32_STD(u_InvalidateRect, 3) { return 1; }
W32_STD(u_SetRect, 5) { uint32_t r[4] = {A(1), A(2), A(3), A(4)}; memcpy(M + A(0), r, 16); return 1; }

// ── WINMM ───────────────────────────────────────────────────────────────────
W32_STD(m_timeGetTime, 0) { return nowMs(); }
W32_STD(m_joyGetNumDevs, 0) { return 0; }
W32_STD(m_joyGetPosEx, 2) { return 0xA7; }
W32_STD(m_joySetThreshold, 2) { return 0; }
W32_STD(m_joySetCapture, 4) { return 0; }
W32_STD(m_joyGetDevCapsA, 3) { return 0; }

struct Mmio { std::vector<uint8_t> data; size_t pos = 0; std::string name; };
static std::map<uint32_t, Mmio> g_mmio;
std::string g_lastWave;

W32_STD(m_mmioOpenA, 3) {
    std::string name = cstr(A(0));
    Mmio m;
    if (!readGameFile(name, m.data)) return 0;
    uint32_t h = newHandle();
    m.name = name;
    g_mmio[h] = std::move(m);
    g_lastWave = name;
    return h;
}
W32_STD(m_mmioClose, 2) { g_mmio.erase(A(0)); return 0; }
W32_STD(m_mmioRead, 3) {
    Mmio& st = g_mmio[A(0)];
    int32_t n = Ai(2);
    size_t take = std::min((size_t)std::max(n, 0), st.data.size() - std::min(st.pos, st.data.size()));
    memcpy(M + A(1), st.data.data() + st.pos, take);
    st.pos += take;
    return (uint32_t)take;
}
static uint32_t le32p(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
W32_STD(m_mmioDescend, 4) {
    Mmio& st = g_mmio[A(0)];
    const auto& d = st.data;
    uint32_t ck = A(1), parent = A(2), flags = A(3);
    size_t end = d.size();
    if (parent) end = std::min(end, (size_t)RD32(parent + 12) + RD32(parent + 4));
    bool wantId = flags & 0x10, wantType = flags & 0x20;
    uint32_t id = RD32(ck), type = RD32(ck + 8);
    size_t pos = st.pos;
    while (pos + 8 <= end) {
        uint32_t cid = le32p(&d[pos]), csz = le32p(&d[pos + 4]);
        uint32_t ctype = pos + 12 <= d.size() ? le32p(&d[pos + 8]) : 0;
        bool ok = true;
        if (wantType) ok = cid == 0x46464952u && ctype == type;
        else if (wantId) ok = cid == id;
        if (ok) {
            bool hasType = cid == 0x46464952u || cid == 0x5453494Cu;
            uint32_t dofs = (uint32_t)pos + 8;
            uint32_t v[5] = {cid, csz, hasType ? ctype : 0, dofs, 0};
            memcpy(M + ck, v, 20);
            st.pos = dofs + (hasType ? 4 : 0);
            return 0;
        }
        pos += 8 + csz + (csz & 1);
    }
    return 0x0101;
}
W32_STD(m_mmioAscend, 3) {
    Mmio& st = g_mmio[A(0)];
    uint32_t dofs = RD32(A(1) + 12), sz = RD32(A(1) + 4);
    st.pos = dofs + sz + (sz & 1);
    return 0;
}

// ── WSOCK32 / misc: networking is not available on the port ─────────────────
W32_STD(n_WSAStartup, 2) { return 10091; }
W32_STD(n_WSACleanup, 0) { return 0; }
W32_STD(n_gethostname, 2) { return 0xFFFFFFFFu; }
W32_STD(n_gethostbyname, 1) { return 0; }
W32_STD(n_socket, 3) { return 0xFFFFFFFFu; }
W32_STD(n_closesocket, 1) { return 0; }
W32_STD(n_htons, 1) { return ((A(0) & 0xff) << 8) | ((A(0) >> 8) & 0xff); }
W32_STD(n_send, 4) { return 0xFFFFFFFFu; }
W32_STD(n_recv, 4) { return 0xFFFFFFFFu; }
W32_STD(n_sendto, 6) { return 0xFFFFFFFFu; }
W32_STD(n_WSAAsyncSelect, 4) { return 0xFFFFFFFFu; }
W32_STD(n_ioctlsocket, 3) { return 0xFFFFFFFFu; }
W32_STD(n_bind, 3) { return 0xFFFFFFFFu; }
W32_STD(n_listen, 2) { return 0xFFFFFFFFu; }
W32_STD(n_connect, 3) { return 0xFFFFFFFFu; }
W32_STD(n_accept, 3) { return 0xFFFFFFFFu; }
W32_STD(n_inet_addr, 1) { return 0xFFFFFFFFu; }
W32_STD(n_inet_ntoa, 1) { return 0; }
W32_STD(n_gethostbyaddr, 3) { return 0; }
// Playback Recording (0x4325c8) opens the Windows file dialog; there is no
// dialog on the Vita, so it behaves as if the player pressed Cancel.
W32_STD(x_GetOpenFileNameA, 1) {
    // Harness only: select the same explicit replay fixture as the oracle.
    const char* path = getenv("LF2_REPLAY_FILE");
    if (!path || !*path) return 0;
    uint32_t ofn = A(0), file = RD32(ofn + 0x1c), cap = RD32(ofn + 0x20);
    size_t len = strlen(path);
    if (!file || len + 1 > cap) return 0;
    memcpy(M + file, path, len + 1);
    return 1;
}
W32_STD(x_ShellExecuteA, 6) { return 0; }
W32_STD(x_CoInitialize, 1) { return 0; }
W32_STD(x_InternetOpenA, 5) { return 0; }
W32_STD(x_InternetOpenUrlA, 6) { return 0; }
W32_STD(x_InternetReadFile, 4) { return 0; }
W32_STD(x_InternetCloseHandle, 1) { return 1; }

// ── MSVCR80 ─────────────────────────────────────────────────────────────────
W32_CDECL(c_srand) { g_randState = A(0); return 0; }
W32_CDECL(c_rand) { g_randState = g_randState * 214013u + 2531011u; return (g_randState >> 16) & 0x7FFF; }
/* differential test (private harness): the rand state is part of the saved game state */
static uint32_t g_savedNow = 100000;
extern "C" void rt_time_save(void) { g_savedNow = g_now; }
extern "C" void rt_time_restore(void) { g_now = g_savedNow; }
static uint32_t g_savedRandState = 1;
extern "C" void rt_rand_save(void) { g_savedRandState = g_randState; }
extern "C" void rt_rand_restore(void) { g_randState = g_savedRandState; }
W32_CDECL(c_zero) { return 0; }
W32_CDECL(c_ident) { return A(0); }
static void c_exit() { host::quit(); }
static void c_watson() { rt::fatal("_invoke_watson"); }
W32_CDECL(c_malloc) { return rt::gameAlloc(A(0), false); }
W32_CDECL(c_calloc) { return rt::gameAlloc(A(0) * A(1), true); }
W32_CDECL(c_free) { rt::gameFree(A(0)); return 0; }
W32_CDECL(c_memset) { if (A(2)) memset(M + A(0), (int)(A(1) & 0xff), A(2)); return A(0); }
W32_CDECL(c_memcpy) { if (A(2)) memmove(M + A(0), M + A(1), A(2)); return A(0); }
W32_CDECL(c_sprintf) {
    std::string s = cFormat(fmtString(A(1)), 2);
    memcpy(M + A(0), s.data(), s.size());
    M[A(0) + s.size()] = 0;
    return (uint32_t)s.size();
}
static void fileWrite(CFile& f, const char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (f.text && p[i] == '\n') {            // text-mode write, as MSVCRT
            if (f.pos >= f.data.size()) f.data.push_back('\r'); else f.data[f.pos] = '\r';
            ++f.pos;
        }
        if (f.pos >= f.data.size()) f.data.push_back((uint8_t)p[i]); else f.data[f.pos] = (uint8_t)p[i];
        ++f.pos;
    }
}
W32_CDECL(c_fprintf) {
    auto it = g_files.find(A(0));
    const std::string& fmt = fmtString(A(1));
    if (fmt.size() == 2 && fmt[0] == '%' && fmt[1] == 'c') {      // decrypt loop fast path
        if (it == g_files.end()) return 0xFFFFFFFFu;
        char c = (char)(A(2) & 0xff);
        fileWrite(it->second, &c, 1);
        return 1;
    }
    std::string s = cFormat(fmt, 2);
    if (it == g_files.end()) return 0xFFFFFFFFu;
    fileWrite(it->second, s.data(), s.size());
    return (uint32_t)s.size();
}
W32_CDECL(c_sscanf) {
    std::string in = cstr(A(0), 1 << 20);
    ScanSrc src{(const uint8_t*)in.data(), in.size(), 0};
    bool eof;
    return (uint32_t)cScan(src, fmtString(A(1)), 2, eof);
}
W32_CDECL(c_fscanf) {
    CFile& f = g_files.at(A(0));
    const std::string& fmt = fmtString(A(1));
    if (fmt.size() == 2 && fmt[0] == '%' && fmt[1] == 'c') {      // cScan("%c") fast path
        if (f.pos >= f.data.size()) { f.eof = true; return 0xFFFFFFFFu; }
        M[A(2)] = f.data[f.pos++];
        return 1;
    }
    ScanSrc src{f.data.data(), f.data.size(), f.pos};
    bool eof;
    int n = cScan(src, fmt, 2, eof);
    f.pos = src.pos;
    if (eof) f.eof = true;
    return (uint32_t)n;
}
W32_CDECL(c_fgets) {
    CFile& f = g_files.at(A(2));
    int32_t n = Ai(1);
    if (f.pos >= f.data.size()) { f.eof = true; return 0; }
    size_t end = f.pos;
    while (end < f.data.size() && f.data[end] != '\n') ++end;
    end = end < f.data.size() ? end + 1 : f.data.size();
    end = std::min(end, f.pos + (size_t)(n - 1));
    size_t len = end - f.pos;
    memcpy(M + A(0), f.data.data() + f.pos, len);
    M[A(0) + len] = 0;
    bool nl = len && f.data[end - 1] == '\n';
    f.pos = end;
    if (f.pos >= f.data.size() && !nl) f.eof = true;
    return A(0);
}
W32_CDECL(c_feof) {
    auto it = g_files.find(A(0));
    return it == g_files.end() || it->second.eof ? 1 : 0;
}
static uint32_t fileOpen(const std::string& name, const std::string& mode) {
    CFile f;
    f.path = name;
    bool text = mode.find('b') == std::string::npos;
    f.text = text;
    if (mode.find('w') != std::string::npos) {
        f.writing = true;
    } else {
        if (!readGameFile(name, f.data)) return 0;
        if (text) {   // CRLF -> LF, Ctrl-Z ends the stream (in place)
            uint8_t* d = f.data.data();
            size_t n = f.data.size(), o = 0;
            if (const void* z = memchr(d, 0x1a, n)) n = (size_t)((const uint8_t*)z - d);
            for (size_t i = 0; i < n; ++i) {
                if (d[i] == '\r' && i + 1 < n && d[i + 1] == '\n') continue;
                d[o++] = d[i];
            }
            f.data.resize(o);
        }
        f.writing = mode.find('a') != std::string::npos;
    }
    uint32_t fp = rt::alloc(32);
    g_files[fp] = std::move(f);
    return fp;
}
W32_CDECL(c_fopen) { return fileOpen(cstr(A(0)), cstr(A(1))); }
static void fileClose(uint32_t h) {
    auto it = g_files.find(h);
    if (it != g_files.end()) {
        if (it->second.writing) {
            std::string k = vfsKey(it->second.path);
            g_vfs[k] = it->second.data;
            if (k != "data\\temporary.txt") host::writeFile(gameRel(it->second.path), it->second.data);
        }
        g_files.erase(it);
    }
}
W32_CDECL(c_fclose) { fileClose(A(0)); return 0; }

// ── native replacement of 0x4148a0 (emit.py NATIVE) ─────────────────────────
// The game's .dat decryptor: fopen(arg,"r"), fopen("data\temporary.txt","w"),
// skip 123 chars with fscanf("%c"), then per char c = (c - key[i]) mod 256 with
// the key "SiuHung..." (i starts at 123 % 37), fprintf("%c", c) until feof;
// fclose both. Same CRT primitives, same FILE allocations and file contents as
// the translated code (which spent ~600 host instructions per byte); only the
// function's own stack frame is not written. Proven against the Unicorn
// oracle by the scenario and long-run replays.
extern "C" void n_dat_decrypt(void) {
    uint32_t name = RD32(R.esp + 4);
    const std::string key = cstr(0x44892c);                 // "SiuHungIsAGoodBear..."
    const size_t len = key.size();
    uint32_t hs = fileOpen(cstr(name), cstr(0x447b30));        // "r"
    uint32_t hd = fileOpen(cstr(0x447b1c), cstr(0x447a70));    // "data\temporary.txt", "w"
    CFile& src = g_files.at(hs);
    CFile& dst = g_files.at(hd);
    auto readc = [&](int& c) {                                 // fscanf(src, "%c", &c)
        if (src.pos >= src.data.size()) { src.eof = true; return; }
        c = src.data[src.pos++];
    };
    size_t i = 0;
    int c = 0;
    for (int k = 0; k < 123; ++k) { i = (i + 1) % len; int skip; readc(skip); }
    readc(c);
    std::vector<char> out;
    out.reserve(src.data.size());
    while (!src.eof) {
        c -= (signed char)key[i];
        if (c < 0) c += 256;
        i = (i + 1) % len;
        out.push_back((char)c);
        c = 0;
        readc(c);
    }
    fileWrite(dst, out.data(), out.size());
    fileClose(hs);
    fileClose(hd);
    R.eax = 0;
    R.esp += 4;                                                // ret
}
// _getcwd(buf, n); buf == NULL -> malloc'd copy (MSVCR80 allocates max(n, len+1))
W32_CDECL(c_getcwd) {
    static const char kCwd[] = "C:\\LF2";
    uint32_t buf = A(0);
    if (!buf) buf = rt::gameAlloc(std::max<uint32_t>(A(1), sizeof kCwd), false);
    wstr(buf, kCwd);
    return buf;
}
static void c_time64() {
    int64_t t = 1246878720;
    if (A(0)) WR64(A(0), (uint64_t)t);
    R.eax = (uint32_t)t; R.edx = (uint32_t)((uint64_t)t >> 32);
    R.esp += 4;
}
W32_CDECL(c_localtime64) {
    if (!g_tm) g_tm = rt::alloc(36);
    int32_t tm[9] = {0, 12, 11, 6, 6, 109, 1, 186, 0};
    memcpy(M + g_tm, tm, 36);
    return g_tm;
}
W32_CDECL(c_pfmode) { if (!g_fmode) g_fmode = rt::alloc(4); return g_fmode; }

// ── MSVCP80 file streams (recordings: 0x43e620 reads, 0x43dd60 writes) ─────
// thiscall: `this` in ECX, callee pops the stack arguments. The constructors
// take a 4th, hidden argument (1 = most derived object: the classes have a
// virtual base), so they pop 4. The game only inspects one field inline: the
// filebuf's FILE* at this+0x54 (nonzero = open). One stream is active at a
// time, so methods act on the current one.
struct Stream { std::vector<uint8_t> data; size_t pos = 0; bool out = false; std::string path; };
static Stream g_stream;
static void sp_ifstream_ctor() {
    uint32_t self = R.ecx;
    std::string name = cstr(A(0));
    g_stream = Stream{};
    g_stream.path = name;
    bool ok = readGameFile(name, g_stream.data);
    WR32(self + 0x54, ok ? 1u : 0u);
    ret(4, self);
}
static void sp_ofstream_ctor() {
    uint32_t self = R.ecx;
    g_stream = Stream{};
    g_stream.path = cstr(A(0));
    g_stream.out = true;
    WR32(self + 0x54, 1u);
    ret(4, self);
}
static void sp_seekg() {                           // seekg(off, dir)
    int32_t off = Ai(0);
    int dir = Ai(1);
    size_t base = dir == 0 ? 0 : dir == 1 ? g_stream.pos : g_stream.data.size();
    long p = (long)base + off;
    g_stream.pos = p < 0 ? 0 : (size_t)p;
    ret(2, R.ecx);
}
static void sp_tellg() {                           // fpos<int> by hidden pointer
    uint32_t r = A(0);
    uint32_t v[6] = {0, 0, (uint32_t)g_stream.pos, 0, 0, 0};
    memcpy(M + r, v, 24);
    ret(1, r);
}
static void sp_read() {                            // read(buf, n)
    uint32_t buf = A(0);
    int32_t n = Ai(1);
    size_t avail = g_stream.pos < g_stream.data.size() ? g_stream.data.size() - g_stream.pos : 0;
    size_t k = std::min((size_t)std::max(n, 0), avail);
    memcpy(M + buf, g_stream.data.data() + g_stream.pos, k);
    g_stream.pos += k;
    ret(2, R.ecx);
}
static void sp_write() {                           // write(buf, n)
    uint32_t buf = A(0);
    int32_t n = Ai(1);
    if (n > 0) g_stream.data.insert(g_stream.data.end(), M + buf, M + buf + n);
    ret(2, R.ecx);
}
static void sp_close() {
    if (g_stream.out && !g_stream.path.empty()) {
        g_vfs[vfsKey(g_stream.path)] = g_stream.data;
        host::writeFile(gameRel(g_stream.path), g_stream.data);
    }
    g_stream.path.clear();
    ret(0, 0);
}
static void sp_dtor() { ret(0, 0); }

// ── binding ─────────────────────────────────────────────────────────────────
struct Binding { const char* name; rt::StubFn fn; };
extern const Binding kGdiBindings[];
extern const Binding kDxBindings[];

static const Binding kBindings[] = {
    {"InterlockedExchange", k_InterlockedExchange}, {"GetVersionExA", k_GetVersionExA},
    {"GetThreadLocale", k_GetThreadLocale}, {"GetLocaleInfoA", k_GetLocaleInfoA},
    {"GetACP", k_GetACP}, {"GetSystemTimeAsFileTime", k_GetSystemTimeAsFileTime},
    {"GetCurrentProcessId", k_GetCurrentProcessId}, {"GetCurrentThreadId", k_GetCurrentThreadId},
    {"GetTickCount", k_GetTickCount}, {"QueryPerformanceCounter", k_QueryPerformanceCounter},
    {"IsDebuggerPresent", k_IsDebuggerPresent},
    {"SetUnhandledExceptionFilter", k_SetUnhandledExceptionFilter},
    {"UnhandledExceptionFilter", k_UnhandledExceptionFilter},
    {"GetCurrentProcess", k_GetCurrentProcess}, {"TerminateProcess", k_TerminateProcess},
    {"GetStartupInfoA", k_GetStartupInfoA}, {"OutputDebugStringA", k_OutputDebugStringA},
    {"lstrlenA", k_lstrlenA}, {"GetModuleHandleA", k_GetModuleHandleA},
    {"CloseHandle", k_CloseHandle}, {"MultiByteToWideChar", k_MultiByteToWideChar},
    {"CreateFileA", k_CreateFileA}, {"Sleep", k_Sleep},
    {"LeaveCriticalSection", k_LeaveCriticalSection}, {"EnterCriticalSection", k_EnterCriticalSection},
    {"GetLocalTime", k_GetLocalTime}, {"GetLastError", k_GetLastError}, {"WriteFile", k_WriteFile},
    {"CreateThread", k_CreateThread}, {"InitializeCriticalSection", k_InitializeCriticalSection},
    {"InterlockedCompareExchange", k_InterlockedCompareExchange},
    {"TranslateMessage", u_TranslateMessage}, {"MessageBoxA", u_MessageBoxA},
    {"DestroyWindow", u_DestroyWindow}, {"UpdateWindow", u_UpdateWindow},
    {"CreateWindowExA", u_CreateWindowExA}, {"RegisterClassA", u_RegisterClassA},
    {"LoadCursorA", u_LoadCursorA}, {"LoadIconA", u_LoadIconA},
    {"GetSystemMetrics", u_GetSystemMetrics}, {"GetKeyState", u_GetKeyState},
    {"PostMessageA", u_PostMessageA}, {"PostQuitMessage", u_PostQuitMessage},
    {"DefWindowProcA", u_DefWindowProcA}, {"SetCursor", u_SetCursor},
    {"ClientToScreen", u_ClientToScreen}, {"GetClientRect", u_GetClientRect},
    {"PeekMessageA", u_PeekMessageA}, {"GetMessageA", u_GetMessageA},
    {"ShowWindow", u_ShowWindow}, {"DispatchMessageA", u_DispatchMessageA},
    {"InvalidateRect", u_InvalidateRect}, {"SetRect", u_SetRect},
    {"timeGetTime", m_timeGetTime}, {"joyGetNumDevs", m_joyGetNumDevs},
    {"joyGetPosEx", m_joyGetPosEx}, {"joySetThreshold", m_joySetThreshold},
    {"joySetCapture", m_joySetCapture}, {"joyGetDevCapsA", m_joyGetDevCapsA},
    {"mmioOpenA", m_mmioOpenA}, {"mmioClose", m_mmioClose}, {"mmioRead", m_mmioRead},
    {"mmioDescend", m_mmioDescend}, {"mmioAscend", m_mmioAscend},
    {"WSAStartup", n_WSAStartup}, {"WSACleanup", n_WSACleanup}, {"gethostname", n_gethostname},
    {"gethostbyname", n_gethostbyname}, {"socket", n_socket}, {"closesocket", n_closesocket},
    {"htons", n_htons}, {"send", n_send}, {"recv", n_recv}, {"sendto", n_sendto},
    {"WSAAsyncSelect", n_WSAAsyncSelect}, {"ioctlsocket", n_ioctlsocket}, {"bind", n_bind},
    {"listen", n_listen}, {"connect", n_connect}, {"accept", n_accept},
    {"inet_addr", n_inet_addr}, {"inet_ntoa", n_inet_ntoa}, {"gethostbyaddr", n_gethostbyaddr},
    {"GetOpenFileNameA", x_GetOpenFileNameA}, {"ShellExecuteA", x_ShellExecuteA},
    {"CoInitialize", x_CoInitialize}, {"InternetOpenA", x_InternetOpenA},
    {"InternetOpenUrlA", x_InternetOpenUrlA}, {"InternetReadFile", x_InternetReadFile},
    {"InternetCloseHandle", x_InternetCloseHandle},
    {"srand", c_srand}, {"rand", c_rand},
    {"_controlfp_s", c_zero}, {"_lock", c_zero}, {"_unlock", c_zero},
    {"_crt_debugger_hook", c_zero}, {"__set_app_type", c_zero}, {"__setusermatherr", c_zero},
    {"_configthreadlocale", c_zero}, {"_ismbblead", c_zero}, {"_cexit", c_zero},
    {"__getmainargs", c_zero}, {"_adjust_fdiv", c_zero}, {"_chdir", c_zero},
    {"_decode_pointer", c_ident}, {"_encode_pointer", c_ident}, {"_onexit", c_ident},
    {"__dllonexit", c_ident},
    {"exit", c_exit}, {"_exit", c_exit}, {"_amsg_exit", c_exit}, {"?terminate@@YAXXZ", c_exit},
    {"_invoke_watson", c_watson},
    {"malloc", c_malloc}, {"??2@YAPAXI@Z", c_malloc}, {"??_U@YAPAXI@Z", c_malloc},
    {"calloc", c_calloc}, {"free", c_free}, {"??3@YAXPAX@Z", c_free},
    {"memset", c_memset}, {"memcpy", c_memcpy}, {"sprintf", c_sprintf},
    {"fprintf", c_fprintf}, {"sscanf", c_sscanf}, {"fscanf", c_fscanf}, {"fgets", c_fgets},
    {"feof", c_feof}, {"fopen", c_fopen}, {"fclose", c_fclose}, {"_getcwd", c_getcwd},
    {"_time64", c_time64}, {"_localtime64", c_localtime64},
    {"__p__fmode", c_pfmode}, {"__p__commode", c_pfmode},
    {"??0?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAE@PBDHH@Z", sp_ifstream_ctor},
    {"??0?$basic_ofstream@DU?$char_traits@D@std@@@std@@QAE@PBDHH@Z", sp_ofstream_ctor},
    {"?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@JH@Z", sp_seekg},
    {"?tellg@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE?AV?$fpos@H@2@XZ", sp_tellg},
    {"?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z", sp_read},
    {"?write@?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV12@PBDH@Z", sp_write},
    {"?close@?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAEXXZ", sp_close},
    {"?close@?$basic_ofstream@DU?$char_traits@D@std@@@std@@QAEXXZ", sp_close},
    {"??_D?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAEXXZ", sp_dtor},
    {"??_D?$basic_ofstream@DU?$char_traits@D@std@@@std@@QAEXXZ", sp_dtor},
};

static rt::StubFn findBinding(const std::string& name) {
    for (const Binding& b : kBindings) if (name == b.name) return b.fn;
    for (const Binding* b = kGdiBindings; b->name; ++b) if (name == b->name) return b->fn;
    for (const Binding* b = kDxBindings; b->name; ++b) if (name == b->name) return b->fn;
    return nullptr;
}

static std::vector<std::string> g_missing;
static void missingImport() { rt::fatal("unimplemented import called"); }

bool bindImports(const std::vector<rt::ImportSlot>& imports) {
    // MSVCR80 data exports live at fixed addresses in the TIB page (the
    // oracle uses the same ones).
    wstr(0x760820, "lf2.exe");
    WR32(0x760800, 0x760820);
    WR32(0x760810, 0);
    for (const rt::ImportSlot& s : imports) {
        rt::StubFn fn = findBinding(s.name);
        if (!fn) { g_missing.push_back(s.name); fn = nullptr; }
        static std::vector<std::string> names;
        names.push_back(s.dll + "!" + s.name);
        uint32_t addr = rt::newStub(strdup(names.back().c_str()), fn);
        if (s.name == "_acmdln") addr = 0x760800;
        else if (s.name == "_adjust_fdiv") addr = 0x760810;
        WR32(s.slot, addr);
    }
    initDirectX();
    for (auto& m : g_missing) host::log("import-missing", m.c_str());
    return true;
}

void runStaticInitializers() {
    const uint32_t tables[2][2] = {{0x4472D8, 0x4472E8}, {0x4472C0, 0x4472D4}};
    for (auto& t : tables)
        for (uint32_t p = t[0]; p < t[1]; p += 4) {
            uint32_t fn = RD32(p);
            if (fn) rt::callGuest(fn, {});
        }
}

void runWinMain() {
    R.esp = GUEST_STACK_TOP - 0x1000;
    rt::callGuest(0x43CF40, {0x400000});
}

}  // namespace win32
