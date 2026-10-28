"""Python implementations of the 163 imports lf2.exe uses.

Scope rule: behave like the Windows/MSVC 8.0 function where the game can
observe the difference (return values, buffer contents, feof timing, text-mode
newline translation, rand() sequence); anything invisible to the game (window
management, cursors, message boxes) is a logged no-op.
"""
from __future__ import annotations

import os
import pathlib
import re
import struct

import numpy as np

from lf2emu import Machine, StopEmu, u32, _Args
from unicorn.x86_const import UC_X86_REG_ECX as X86_ECX


# ─────────────────────────────────────────────────────────────────────────────
#  BMP decoding (files and RT_BITMAP resources)
# ─────────────────────────────────────────────────────────────────────────────
def decode_dib(d: bytes, off_bits: int | None = None) -> np.ndarray:
    """Decode a BITMAPINFOHEADER DIB (d starts at the header) -> uint32 XRGB."""
    hsz, w, h, planes, bpp, comp = struct.unpack_from("<IiiHHI", d, 0)
    clr_used = struct.unpack_from("<I", d, 32)[0] if hsz >= 36 else 0
    topdown = h < 0
    h = abs(h)
    ncol = clr_used or (1 << bpp if bpp <= 8 else 0)
    pal = np.zeros(256, dtype=np.uint32)
    for i in range(ncol):
        b, g, r, _ = d[hsz + 4 * i: hsz + 4 * i + 4]
        pal[i] = (r << 16) | (g << 8) | b
    if off_bits is None:
        off_bits = hsz + 4 * ncol
    px = d[off_bits:]
    img = np.zeros((h, w), dtype=np.uint32)
    if bpp == 8 and comp == 1:                       # RLE8
        idx = np.zeros((h, w), dtype=np.uint8)
        x = y = 0
        i = 0
        n = len(px)
        while i + 1 < n and y < h:
            c, v = px[i], px[i + 1]
            i += 2
            if c > 0:
                if y < h:
                    e = min(w, x + c)
                    if x < w:
                        idx[y, x:e] = v
                x += c
            elif v == 0:
                x = 0; y += 1
            elif v == 1:
                break
            elif v == 2:
                x += px[i]; y += px[i + 1]; i += 2
            else:
                run = px[i:i + v]
                if y < h and x < w:
                    e = min(w, x + v)
                    idx[y, x:e] = np.frombuffer(run[:e - x], dtype=np.uint8)
                x += v
                i += v + (v & 1)
        img = pal[idx]
        if not topdown:
            img = img[::-1]
    elif bpp == 8:
        stride = (w + 3) & ~3
        a = np.frombuffer(px[:stride * h], dtype=np.uint8).reshape(h, stride)[:, :w]
        img = pal[a]
        if not topdown:
            img = img[::-1]
    elif bpp == 24:
        stride = (w * 3 + 3) & ~3
        a = np.frombuffer(px[:stride * h], dtype=np.uint8).reshape(h, stride)[:, :w * 3]
        a = a.reshape(h, w, 3).astype(np.uint32)
        img = (a[:, :, 2] << 16) | (a[:, :, 1] << 8) | a[:, :, 0]
        if not topdown:
            img = img[::-1]
    elif bpp == 4:
        stride = ((w + 1) // 2 + 3) & ~3
        a = np.frombuffer(px[:stride * h], dtype=np.uint8).reshape(h, stride)
        hi = a >> 4
        lo = a & 15
        both = np.stack([hi, lo], axis=2).reshape(h, stride * 2)[:, :w]
        img = pal[both]
        if not topdown:
            img = img[::-1]
    else:
        raise ValueError("unsupported DIB bpp=%d comp=%d" % (bpp, comp))
    return np.ascontiguousarray(img)


_FONT = None


def gdi_font():
    """Glyphs of src/recomp/gdi_font.h (the port draws TextOut with them)."""
    global _FONT
    if _FONT is None:
        txt = (pathlib.Path(__file__).resolve().parents[2] / "src/recomp/gdi_font.h").read_text()
        H = int(re.search(r"constexpr int H = (\d+);", txt).group(1))
        W = [int(v) for v in re.search(r"W\[256\] = \{([^}]*)\}", txt).group(1).split(",")]
        rows = [int(v, 16) for v in re.findall(r"0x[0-9a-f]{4}", txt.split("ROWS")[1])]
        _FONT = (W, rows, H)
    return _FONT


def decode_bmp_file(d: bytes) -> np.ndarray:
    assert d[:2] == b"BM"
    off = struct.unpack_from("<I", d, 10)[0]
    return decode_dib(d[14:], off - 14)


# ─────────────────────────────────────────────────────────────────────────────
#  printf / scanf
# ─────────────────────────────────────────────────────────────────────────────
_FMT = re.compile(r"%([-+ #0]*)(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|L|I64|I32|I)?([diouxXeEfgGcspn%])")


def c_format(m: Machine, fmt: str, args: _Args, k: int) -> str:
    out = []
    pos = 0
    for mt in _FMT.finditer(fmt):
        out.append(fmt[pos:mt.start()])
        pos = mt.end()
        flags, width, prec, length, conv = mt.groups()
        if conv == "%":
            out.append("%")
            continue
        if width == "*":
            width = str(args.i(k)); k += 1
        if prec == "*":
            prec = str(args.i(k)); k += 1
        spec = "%" + flags + (width or "") + ("." + prec if prec is not None else "")
        if conv in "di":
            if length == "I64" or length == "ll":
                v = struct.unpack("<q", struct.pack("<II", args[k], args[k + 1]))[0]; k += 2
            else:
                v = args.i(k); k += 1
            out.append((spec + "d") % v)
        elif conv in "uoxX":
            if length == "I64" or length == "ll":
                v = args[k] | (args[k + 1] << 32); k += 2
            else:
                v = args[k]; k += 1
            out.append((spec + conv) % v)
        elif conv in "eEfgG":
            v = args.f64(k); k += 2
            out.append((spec + conv) % v)
        elif conv == "c":
            out.append((spec + "c") % chr(args[k] & 0xFF)); k += 1
        elif conv == "s":
            s = m.cstr(args[k]); k += 1
            out.append((spec + "s") % ("(null)" if s is None else s))
        elif conv == "p":
            out.append("%08X" % args[k]); k += 1
        else:
            raise NotImplementedError(conv)
    out.append(fmt[pos:])
    return "".join(out)


class ScanSource:
    """Character source for scanf over a string or an emulated FILE."""

    def __init__(self, data: bytes, pos: int = 0):
        self.data, self.pos = data, pos

    def peek(self):
        return self.data[self.pos] if self.pos < len(self.data) else None

    def get(self):
        c = self.peek()
        if c is not None:
            self.pos += 1
        return c


_WS = b" \t\n\r\v\f"


def c_scan(m: Machine, src: ScanSource, fmt: str, args: _Args, k: int):
    """MSVC scanf subset. Returns (assigned, hit_eof)."""
    assigned = 0
    i = 0
    n = len(fmt)
    eof = False
    started = False
    while i < n:
        c = fmt[i]
        if c in " \t\n\r\v\f":
            while src.peek() is not None and src.peek() in _WS:
                src.get()
            i += 1
            continue
        if c != "%":
            ch = src.peek()
            if ch is None:
                eof = True
                break
            if ch != ord(c):
                break
            src.get(); i += 1
            continue
        i += 1
        suppress = False
        if i < n and fmt[i] == "*":
            suppress = True; i += 1
        width = 0
        while i < n and fmt[i].isdigit():
            width = width * 10 + int(fmt[i]); i += 1
        length = ""
        while i < n and fmt[i] in "hlLI":
            length += fmt[i]; i += 1
        conv = fmt[i]; i += 1
        if conv not in "c[n":
            while src.peek() is not None and src.peek() in _WS:
                src.get()
        if conv == "n":
            if not suppress:
                m.w32(args[k], src.pos); k += 1
            continue
        if src.peek() is None:
            eof = True
            break
        started = True
        if conv == "s":
            buf = bytearray()
            while src.peek() is not None and src.peek() not in _WS and (not width or len(buf) < width):
                buf.append(src.get())
            if src.peek() is None:
                eof = True               # MSVC: the read that ended the token hit EOF
            if not suppress:
                m.write(args[k], bytes(buf) + b"\0"); k += 1; assigned += 1
        elif conv == "c":
            w = width or 1
            buf = bytearray()
            while len(buf) < w and src.peek() is not None:
                buf.append(src.get())
            if not suppress:
                m.write(args[k], bytes(buf)); k += 1; assigned += 1
        elif conv in "dixuo":
            buf = bytearray()
            allowed = b"0123456789abcdefABCDEFxX" if conv in "xi" else b"0123456789"
            if src.peek() in b"+-":
                buf.append(src.get())
            while src.peek() is not None and src.peek() in allowed and (not width or len(buf) < width):
                buf.append(src.get())
            if src.peek() is None:
                eof = True
            s = buf.decode()
            try:
                if conv == "x":
                    v = int(s, 16)
                elif conv == "i":
                    v = int(s, 0)
                elif conv == "o":
                    v = int(s, 8)
                else:
                    v = int(s)
            except ValueError:
                break
            if not suppress:
                if length in ("h",):
                    m.w16(args[k], v)
                else:
                    m.w32(args[k], v)
                k += 1; assigned += 1
        elif conv in "feEgG":
            buf = bytearray()
            fl = b"0123456789+-.eE"
            while src.peek() is not None and src.peek() in fl and (not width or len(buf) < width):
                ch = src.get()
                if ch in b"+-" and buf and buf[-1] not in b"eE":
                    src.pos -= 1
                    break
                buf.append(ch)
            if src.peek() is None:
                eof = True
            try:
                v = float(buf.decode())
            except ValueError:
                break
            if not suppress:
                if length in ("l", "L"):
                    m.wf64(args[k], v)
                else:
                    m.write(args[k], struct.pack("<f", v))
                k += 1; assigned += 1
        else:
            raise NotImplementedError("scanf %" + conv)
    if not started and eof:
        return -1, True
    return assigned, eof


# ─────────────────────────────────────────────────────────────────────────────
#  The machine with a Windows personality
# ─────────────────────────────────────────────────────────────────────────────
class CFile:
    def __init__(self, path, data, mode):
        self.path, self.mode = path, mode
        self.text = "b" not in mode
        if self.text and data is not None:
            data = data.replace(b"\r\n", b"\n")
            z = data.find(b"\x1a")          # text-mode Ctrl-Z ends the stream
            if z >= 0:
                data = data[:z]
        self.data = bytearray(data or b"")
        self.pos = 0
        self.eof = False
        self.writing = "w" in mode or "a" in mode


class Surface:
    def __init__(self, w, h, caps):
        self.w, self.h, self.caps = w, h, caps
        self.pix = np.zeros((h, w), dtype=np.uint32)
        self.ck = None               # source color key (lo, hi)
        self.lock_mem = None


class SoundBuf:
    def __init__(self, name, size, fmt):
        self.name, self.size, self.fmt = name, size, fmt
        self.mem = None


DDRAW_METHODS = {
    # IDirectDraw v1
    "dd": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "Compact", 1, "CreateClipper", 4,
           "CreatePalette", 5, "CreateSurface", 4, "DuplicateSurface", 3, "EnumDisplayModes", 5,
           "EnumSurfaces", 5, "FlipToGDISurface", 1, "GetCaps", 3, "GetDisplayMode", 2,
           "GetFourCCCodes", 3, "GetGDISurface", 2, "GetMonitorFrequency", 2, "GetScanLine", 2,
           "GetVerticalBlankStatus", 2, "Initialize", 2, "RestoreDisplayMode", 1,
           "SetCooperativeLevel", 3, "SetDisplayMode", 4, "WaitForVerticalBlank", 3],
    "surf": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "AddAttachedSurface", 2,
             "AddOverlayDirtyRect", 2, "Blt", 6, "BltBatch", 4, "BltFast", 6,
             "DeleteAttachedSurface", 3, "EnumAttachedSurfaces", 3, "EnumOverlayZOrders", 4,
             "Flip", 3, "GetAttachedSurface", 3, "GetBltStatus", 2, "GetCaps", 2, "GetClipper", 2,
             "GetColorKey", 3, "GetDC", 2, "GetFlipStatus", 2, "GetOverlayPosition", 3,
             "GetPalette", 2, "GetPixelFormat", 2, "GetSurfaceDesc", 2, "Initialize", 3,
             "IsLost", 1, "Lock", 5, "ReleaseDC", 2, "Restore", 1, "SetClipper", 2,
             "SetColorKey", 3, "SetOverlayPosition", 3, "SetPalette", 2, "Unlock", 2,
             "UpdateOverlay", 6, "UpdateOverlayDisplay", 2, "UpdateOverlayZOrder", 3],
    "clip": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "GetClipList", 4, "GetHWnd", 2,
             "Initialize", 3, "IsClipListChanged", 2, "SetClipList", 3, "SetHWnd", 3],
    "ds": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "CreateSoundBuffer", 4, "GetCaps", 2,
           "DuplicateSoundBuffer", 3, "SetCooperativeLevel", 3, "Compact", 1,
           "GetSpeakerConfig", 2, "SetSpeakerConfig", 2, "Initialize", 2],
    "graph": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "AddFilter", 3, "RemoveFilter", 2,
              "EnumFilters", 2, "FindFilterByName", 3, "ConnectDirect", 4, "Reconnect", 2,
              "Disconnect", 2, "SetDefaultSyncSource", 1, "Connect", 3, "Render", 2,
              "RenderFile", 3, "AddSourceFilter", 4, "SetLogFile", 2, "Abort", 1,
              "ShouldOperationContinue", 1],
    "mctrl": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "GetTypeInfoCount", 2, "GetTypeInfo", 4, "GetIDsOfNames", 6, "Invoke", 9,
              "Run", 1, "Pause", 1, "Stop", 1, "GetState", 3, "RenderFile", 2,
              "AddSourceFilter", 3, "get_FilterCollection", 2, "get_RegFilterCollection", 2,
              "StopWhenReady", 1],
    "mevent": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "GetTypeInfoCount", 2, "GetTypeInfo", 4, "GetIDsOfNames", 6, "Invoke", 9,
               "GetEventHandle", 2, "GetEvent", 5, "WaitForCompletion", 3,
               "CancelDefaultHandling", 2, "RestoreDefaultHandling", 2, "FreeEventParams", 4,
               "SetNotifyWindow", 4, "SetNotifyFlags", 2, "GetNotifyFlags", 2],
    "mpos": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "GetTypeInfoCount", 2, "GetTypeInfo", 4, "GetIDsOfNames", 6, "Invoke", 9,
             "get_Duration", 2, "put_CurrentPosition", 3, "get_CurrentPosition", 2,
             "get_StopTime", 2, "put_StopTime", 3, "get_PrerollTime", 2, "put_PrerollTime", 3,
             "put_Rate", 3, "get_Rate", 2, "CanSeekForward", 2, "CanSeekBackward", 2],
    "baudio": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "GetTypeInfoCount", 2, "GetTypeInfo", 4, "GetIDsOfNames", 6, "Invoke", 9,
               "put_Volume", 2, "get_Volume", 2, "put_Balance", 2, "get_Balance", 2],
    "dsb": ["QueryInterface", 3, "AddRef", 1, "Release", 1, "GetCaps", 2, "GetCurrentPosition", 3,
            "GetFormat", 4, "GetVolume", 2, "GetPan", 2, "GetFrequency", 2, "GetStatus", 2,
            "Initialize", 3, "Lock", 8, "Play", 4, "SetCurrentPosition", 2, "SetFormat", 2,
            "SetVolume", 2, "SetPan", 2, "SetFrequency", 2, "Stop", 1, "Unlock", 5, "Restore", 1],
}

SCREEN_W, SCREEN_H = 794, 550
E_FAIL = 0x80004005


class LF2Machine(Machine):
    def __init__(self, game_dir):
        super().__init__()
        self.game = pathlib.Path(game_dir)
        self.vfs = {}                  # in-memory files written by the game
        self.files = {}                # FILE* -> CFile
        self.handles = {}              # generic handle table
        self.next_handle = 0x1000
        self.now = 100000              # virtual timeGetTime (ms)
        self.rand_state = 1
        self.vtables = {}
        self.objs = {}                 # COM object address -> python object
        self.surfaces = {}
        self.sounds = []               # (frame, buffer name, volume, pan)
        self.frame = 0
        self.dsbufs = {}
        self.dcs = {}                  # hdc -> surface | bitmap
        self.bitmaps = {}              # hbitmap -> np image
        self.texts = []                # (frame, x, y, text)
        self.primary = None
        self.back = None
        self.last_wave = None
        self.mmio = {}
        self.messages = []
        self.msgq = []
        self.wndproc = 0
        self.music = []
        self.dc_colors = {}
        self.free_lists = {}
        self.block_size = {}
        self._res = self._index_resources()
        self._bind_all()

    # ── helpers ──────────────────────────────────────────────────────────────
    def handle(self, obj):
        h = self.next_handle
        self.next_handle += 4
        self.handles[h] = obj
        return h

    def gpath(self, p: str) -> pathlib.Path:
        p = p.replace("\\", "/")
        return self.game / p

    @staticmethod
    def game_rel(p: str) -> str:
        """Absolute paths built from _getcwd() ("C:\\LF2") name game files."""
        if len(p) > 7 and p[:7].lower() == "c:\\lf2\\":
            return p[7:]
        return p

    def read_file(self, p: str):
        p = self.game_rel(p)
        key = p.replace("/", "\\").lower()
        if key in self.vfs:
            return bytes(self.vfs[key])
        fp = self.gpath(p)
        # case-insensitive lookup
        if not fp.exists():
            parent = fp.parent
            if parent.exists():
                for c in parent.iterdir():
                    if c.name.lower() == fp.name.lower():
                        fp = c
                        break
        if fp.exists() and fp.is_file():
            return fp.read_bytes()
        return None

    def _index_resources(self):
        res = {}
        pe = self.pe
        for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
            if t.id != 2:
                continue
            for e in t.directory.entries:
                lang = e.directory.entries[0]
                d = lang.data.struct
                data = pe.get_data(d.OffsetToData, d.Size)
                res[str(e.name).upper()] = data
        return res

    # ── COM plumbing ─────────────────────────────────────────────────────────
    def com_object(self, kind, pyobj, extra=0x40):
        if kind not in self.vtables:
            spec = DDRAW_METHODS[kind]
            vt = self.alloc(4 * (len(spec) // 2))
            for i in range(0, len(spec), 2):
                name, nargs = spec[i], spec[i + 1]
                h = getattr(self, "%s_%s" % (kind, name), None)
                if h is None:
                    h = self._com_default(kind, name)
                addr = self.new_stub("%s::%s" % (kind, name), h, nargs)
                self.w32(vt + 2 * i, addr)
            self.vtables[kind] = vt
        obj = self.alloc(extra)
        self.w32(obj, self.vtables[kind])
        self.objs[obj] = pyobj
        return obj

    def _com_default(self, kind, name):
        def h(a):
            return 0
        return h

    # ── binding table ────────────────────────────────────────────────────────
    def _bind_all(self):
        B = self.bind
        std = {
            # KERNEL32
            "InterlockedExchange": 2, "GetVersionExA": 1, "GetThreadLocale": 0,
            "GetLocaleInfoA": 4, "GetACP": 0, "GetSystemTimeAsFileTime": 1,
            "GetCurrentProcessId": 0, "GetCurrentThreadId": 0, "GetTickCount": 0,
            "QueryPerformanceCounter": 1, "IsDebuggerPresent": 0,
            "SetUnhandledExceptionFilter": 1, "UnhandledExceptionFilter": 1,
            "GetCurrentProcess": 0, "TerminateProcess": 2, "GetStartupInfoA": 1,
            "OutputDebugStringA": 1, "lstrlenA": 1, "GetModuleHandleA": 1, "CloseHandle": 1,
            "MultiByteToWideChar": 6, "CreateFileA": 7, "Sleep": 1,
            "LeaveCriticalSection": 1, "EnterCriticalSection": 1, "GetLocalTime": 1,
            "GetLastError": 0, "WriteFile": 5, "CreateThread": 6,
            "InitializeCriticalSection": 1, "InterlockedCompareExchange": 3,
            # USER32
            "TranslateMessage": 1, "LoadImageA": 6, "MessageBoxA": 4, "DestroyWindow": 1,
            "UpdateWindow": 1, "CreateWindowExA": 12, "RegisterClassA": 1, "LoadCursorA": 2,
            "LoadIconA": 2, "GetSystemMetrics": 1, "GetKeyState": 1, "PostMessageA": 4,
            "PostQuitMessage": 1, "DefWindowProcA": 4, "SetCursor": 1, "ClientToScreen": 2,
            "GetClientRect": 2, "PeekMessageA": 5, "GetMessageA": 4, "ShowWindow": 2,
            "DispatchMessageA": 1, "InvalidateRect": 3, "SetRect": 5,
            # GDI32
            "DeleteObject": 1, "SelectObject": 2, "GetObjectA": 3, "StretchBlt": 11,
            "DeleteDC": 1, "SetBkColor": 2, "CreateCompatibleDC": 1, "SetTextColor": 2,
            "TextOutA": 5,
            # WINMM
            "joyGetNumDevs": 0, "mmioAscend": 3, "mmioRead": 3, "joyGetPosEx": 2,
            "joySetThreshold": 2, "joySetCapture": 4, "joyGetDevCapsA": 3, "timeGetTime": 0,
            "mmioOpenA": 3, "mmioDescend": 4, "mmioClose": 2,
            "DirectDrawCreate": 3, "#1": 3,
            # WSOCK32 (ordinal-less names)
            "inet_ntoa": 1, "gethostbyname": 1, "socket": 3, "htons": 1, "WSAAsyncSelect": 4,
            "bind": 3, "listen": 2, "inet_addr": 1, "gethostbyaddr": 3, "connect": 3,
            "ioctlsocket": 3, "accept": 3, "send": 4, "recv": 4, "WSACleanup": 0, "sendto": 6,
            "closesocket": 1, "WSAStartup": 2, "gethostname": 2,
            "GetOpenFileNameA": 1, "ShellExecuteA": 6, "CoInitialize": 1,
            "CoCreateInstance": 5,
            "InternetCloseHandle": 1, "InternetOpenA": 5, "InternetOpenUrlA": 6,
            "InternetReadFile": 4,
        }
        for name, n in std.items():
            if name not in self.iat:
                continue
            h = getattr(self, "api_" + name.replace("#", "ord"), None)
            if h is None:
                h = self._default_api(name)
            B(name, h, n)
        crt = ["srand", "_controlfp_s", "_invoke_watson", "_decode_pointer", "_onexit", "_lock",
               "__dllonexit", "_unlock", "_crt_debugger_hook", "__set_app_type",
               "_encode_pointer", "__p__fmode", "__p__commode", "_adjust_fdiv",
               "__setusermatherr", "_configthreadlocale", "_initterm_e", "_initterm", "exit",
               "_ismbblead", "_XcptFilter", "_exit", "_cexit", "__getmainargs", "_amsg_exit",
               "sscanf", "_except_handler4_common", "memset", "memcpy", "sprintf",
               "__CxxFrameHandler3", "free", "fprintf", "fclose", "fscanf", "feof", "fopen",
               "malloc", "rand", "fgets", "_chdir", "_getcwd", "_localtime64", "_time64",
               "calloc"]
        alias = {"??3@YAXPAX@Z": "free", "??2@YAPAXI@Z": "malloc", "??_U@YAPAXI@Z": "malloc",
                 "?terminate@@YAXXZ": "exit"}
        for name in crt + list(alias):
            if name not in self.iat:
                continue
            impl = alias.get(name, name)
            h = getattr(self, "crt_" + impl, None)
            if h is None:
                h = self._default_api(name)
            B(name, h, None)
        # MSVCP80 ifstream/ofstream (thiscall, callee cleans)
        # MSVCR80 data exports: the IAT slot must point at real memory.
        # Same fixed addresses as src/recomp/win32_api.cpp (TIB page).
        self.w32(0x760820, 0)
        self.wstr(0x760820, "lf2.exe")
        self.w32(0x760800, 0x760820)
        self.w32(0x760810, 0)
        for name, addr in (("_acmdln", 0x760800), ("_adjust_fdiv", 0x760810)):
            if name in self.iat:
                for slot, n in self._iat_slots():
                    if n == name:
                        self.w32(slot, addr)
        for name in self.iat:
            if "basic_" in name:
                B(name, self._stream_api(name), self._msvcp_nargs(name))

    def _stream_api(self, name):
        ecx = lambda: self.u.reg_read(X86_ECX)
        if name.startswith("??0?$basic_ifstream"):
            def h(a):
                self.stream = {"path": self.cstr(a[0]), "out": False, "pos": 0}
                data = self.read_file(self.stream["path"])
                self.stream["data"] = bytearray(data or b"")
                self.w32(ecx() + 0x54, 1 if data is not None else 0)
                return ecx()
        elif name.startswith("??0?$basic_ofstream"):
            def h(a):
                self.stream = {"path": self.cstr(a[0]), "out": True, "pos": 0, "data": bytearray()}
                self.w32(ecx() + 0x54, 1)
                return ecx()
        elif name.startswith("?seekg"):
            def h(a):
                st = self.stream
                base = {0: 0, 1: st["pos"]}.get(a.i(1), len(st["data"]))
                st["pos"] = max(0, base + a.i(0))
                return ecx()
        elif name.startswith("?tellg"):
            def h(a):
                self.write(a[0], struct.pack("<6I", 0, 0, self.stream["pos"], 0, 0, 0))
                return a[0]
        elif name.startswith("?read"):
            def h(a):
                st = self.stream
                n = max(a.i(1), 0)
                chunk = bytes(st["data"][st["pos"]:st["pos"] + n])
                self.write(a[0], chunk)
                st["pos"] += len(chunk)
                return ecx()
        elif name.startswith("?write"):
            def h(a):
                if a.i(1) > 0:
                    self.stream["data"] += self.read(a[0], a.i(1))
                return ecx()
        elif name.startswith("?close"):
            def h(a):
                st = getattr(self, "stream", None)
                if st and st["out"] and st["path"]:
                    self.vfs[self.game_rel(st["path"]).replace("/", "\\").lower()] = bytes(st["data"])
                    directory = os.environ.get("LF2_WRITE_DIR")
                    if directory:
                        name = self.game_rel(st["path"]).replace("/", "_").replace("\\", "_").replace(":", "_")
                        pathlib.Path(directory, name).write_bytes(st["data"])
                if st:
                    st["path"] = ""
                return 0
        else:
            def h(a):
                return 0
        return h

    def _iat_slots(self):
        for entry in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in entry.imports:
                yield imp.address, (imp.name.decode() if imp.name else "#%d" % imp.ordinal)

    def _msvcp_nargs(self, name):
        if name.startswith("??0"):
            return 4                    # + hidden "most derived" flag (virtual base)
        if name.startswith("?seekg"):
            return 2
        if name.startswith("?tellg"):
            return 1
        if name.startswith("?read") or name.startswith("?write"):
            return 2
        return 0

    def _default_api(self, name):
        def h(a):
            raise RuntimeError("unimplemented API %s" % name)
        return h

    # ── KERNEL32 ─────────────────────────────────────────────────────────────
    def api_InterlockedExchange(self, a):
        old = self.r32(a[0]); self.w32(a[0], a[1]); return old
    def api_InterlockedCompareExchange(self, a):
        old = self.r32(a[0])
        if old == a[2]:
            self.w32(a[0], a[1])
        return old
    def api_GetVersionExA(self, a): return 1
    def api_GetThreadLocale(self, a): return 0x409
    def api_GetLocaleInfoA(self, a): return 0
    def api_GetACP(self, a): return 1252
    def api_GetSystemTimeAsFileTime(self, a): self.write(a[0], b"\0" * 8)
    def api_GetCurrentProcessId(self, a): return 4
    def api_GetCurrentThreadId(self, a): return 8
    def api_GetTickCount(self, a): return self.now
    def api_QueryPerformanceCounter(self, a):
        self.write(a[0], struct.pack("<Q", self.now * 1000)); return 1
    def api_IsDebuggerPresent(self, a): return 0
    def api_SetUnhandledExceptionFilter(self, a): return 0
    def api_UnhandledExceptionFilter(self, a): return 0
    def api_GetCurrentProcess(self, a): return 0xFFFFFFFF
    def api_TerminateProcess(self, a): raise StopEmu()
    def api_GetStartupInfoA(self, a): self.write(a[0], struct.pack("<I", 68) + b"\0" * 64)
    def api_OutputDebugStringA(self, a): self.messages.append(("dbg", self.cstr(a[0])))
    def api_lstrlenA(self, a): return len(self.cstr(a[0]) or "")
    def api_GetModuleHandleA(self, a): return 0x400000 if a[0] == 0 else 0
    def api_CloseHandle(self, a): return 1
    def api_MultiByteToWideChar(self, a): return 0
    def api_CreateFileA(self, a): return 0xFFFFFFFF
    def api_Sleep(self, a): self.now += a[0]
    def api_LeaveCriticalSection(self, a): return 0
    def api_EnterCriticalSection(self, a): return 0
    def api_InitializeCriticalSection(self, a): return 0
    def api_GetLocalTime(self, a):
        self.write(a[0], struct.pack("<8H", 2009, 7, 6, 11, 12, 0, 0, 0))
    def api_GetLastError(self, a): return 0
    def api_WriteFile(self, a): return 1
    def api_CreateThread(self, a):
        self.messages.append(("thread", hex(a[2]))); return self.handle("thread")

    # ── USER32 ───────────────────────────────────────────────────────────────
    def api_TranslateMessage(self, a): return 0

    def post(self, msg, wparam=0, lparam=0):
        self.msgq.append((msg, wparam, lparam))

    def _fill_msg(self, p, m):
        self.write(p, struct.pack("<IIIIIii", 0x5000, m[0], m[1] & 0xFFFFFFFF,
                                  m[2] & 0xFFFFFFFF, self.now, 0, 0))

    def api_PeekMessageA(self, a):
        if self.msgq:
            if a[0]:
                self._fill_msg(a[0], self.msgq[0])
            if a[4] & 1:
                self.msgq.pop(0)
            return 1
        self.now += 1          # one idle message-loop pass costs 1 ms of virtual time
        return 0

    def api_GetMessageA(self, a):
        if not self.msgq:
            raise StopEmu()
        m = self.msgq.pop(0)
        self._fill_msg(a[0], m)
        return 0 if m[0] == 0x12 else 1

    def api_DispatchMessageA(self, a):
        # Enter the WndProc as if DispatchMessageA had called it: stdcall
        # WndProc(hwnd,msg,wp,lp) pops 16 bytes and returns straight to the
        # caller of DispatchMessageA with DispatchMessageA's own argument gone.
        hwnd, msg, wp, lp = struct.unpack("<IIII", self.read(a[0], 16))
        esp = a.addr(0) - 4          # points at DispatchMessageA's return address
        ret = self.r32(esp)
        new = esp + 8 - 20
        self.write(new, struct.pack("<IIIII", ret, hwnd, msg, wp, lp))
        return {"eip": self.wndproc, "esp": new}
    def api_MessageBoxA(self, a):
        self.messages.append(("msgbox", self.cstr(a[1]), self.cstr(a[2]))); return 1
    def api_DestroyWindow(self, a): return 1
    def api_UpdateWindow(self, a): return 1
    def api_CreateWindowExA(self, a): return 0x5000
    def api_RegisterClassA(self, a):
        self.wndproc = self.r32(a[0] + 4)
        return 1
    def api_LoadCursorA(self, a): return 0x6000
    def api_LoadIconA(self, a): return 0x6004
    def api_GetSystemMetrics(self, a): return {0: 1024, 1: 768}.get(a[0], 0)
    def api_GetKeyState(self, a): return 0
    def api_PostMessageA(self, a): return 1
    def api_PostQuitMessage(self, a): return 0
    def api_DefWindowProcA(self, a): return 0
    def api_SetCursor(self, a): return 0
    def api_ClientToScreen(self, a): return 1
    def api_GetClientRect(self, a):
        self.write(a[1], struct.pack("<4i", 0, 0, SCREEN_W, SCREEN_H)); return 1
    def api_ShowWindow(self, a):
        # Windows sends WM_MOVE on the first show; the WndProc derives the
        # blit rectangle of the windowed present (DAT_00453ccc) from it.
        if not getattr(self, "_shown", False):
            self._shown = True
            self.post(0x3, 0, 0)
        return 1
    def api_InvalidateRect(self, a): return 1
    def api_SetRect(self, a):
        self.write(a[0], struct.pack("<4I", a[1], a[2], a[3], a[4])); return 1

    def api_LoadImageA(self, a):
        hinst, name_p, typ, cx, cy, flags = a[0], a[1], a[2], a[3], a[4], a[5]
        name = self.cstr(name_p) if name_p > 0xFFFF else None
        if flags & 0x10:                     # LR_LOADFROMFILE
            data = self.read_file(name) if name else None
            if data is None or data[:2] != b"BM":
                return 0
            img = decode_bmp_file(data)
        else:
            data = self._res.get((name or "").upper())
            if data is None:
                return 0
            img = decode_dib(data)
        h = self.handle(("bmp", name))
        self.bitmaps[h] = img
        return h

    # ── GDI32 ────────────────────────────────────────────────────────────────
    def api_GetObjectA(self, a):
        img = self.bitmaps.get(a[0])
        if img is None:
            return 0
        h, w = img.shape
        self.write(a[2], struct.pack("<iiiiHHI", 0, w, h, ((w * 3 + 3) & ~3), 1, 24, 0))
        return 24
    def api_CreateCompatibleDC(self, a):
        h = self.handle("dc"); self.dcs[h] = None; return h
    def api_SelectObject(self, a):
        if a[1] in self.bitmaps:
            self.dcs[a[0]] = ("bmp", a[1])
        return 1
    def api_DeleteDC(self, a):
        self.dcs.pop(a[0], None); return 1
    def api_DeleteObject(self, a):
        self.bitmaps.pop(a[0], None); return 1
    def api_SetBkColor(self, a):
        self.dc_colors.setdefault(a[0], [0, 0xFFFFFF])[1] = a[1]; return 0
    def api_SetTextColor(self, a):
        self.dc_colors.setdefault(a[0], [0, 0xFFFFFF])[0] = a[1]; return 0
    def api_TextOutA(self, a):
        raw = self.read(a[3], a[4]) if a[4] else b""
        self.texts.append((self.frame, a.i(1), a.i(2), raw.decode("latin1")))
        dc = self.dcs.get(a[0])
        if dc and dc[0] == "surf":
            fg, bg = self.dc_colors.get(a[0], [0, 0xFFFFFF])
            fg = ((fg & 0xff) << 16) | (fg & 0xff00) | ((fg >> 16) & 0xff)
            bg = ((bg & 0xff) << 16) | (bg & 0xff00) | ((bg >> 16) & 0xff)
            surf = self.objs_surface(dc[1])
            W, ROWS, H = gdi_font()
            x0, y0 = a.i(1), a.i(2)
            for yy in range(H):
                py = y0 + yy
                if not 0 <= py < surf.h:
                    continue
                px = x0
                for c in raw:
                    bits = ROWS[c * H + yy]
                    for b in range(W[c]):
                        if 0 <= px < surf.w:
                            surf.pix[py, px] = fg if (bits >> b) & 1 else bg
                        px += 1
        return 1

    def api_StretchBlt(self, a):
        dd, dx, dy, dw, dh = a[0], a.i(1), a.i(2), a.i(3), a.i(4)
        sd, sx, sy, sw, sh = a[5], a.i(6), a.i(7), a.i(8), a.i(9)
        dst = self.dcs.get(dd)
        src = self.dcs.get(sd)
        if not dst or not src or dst[0] != "surf" or src[0] != "bmp":
            return 1
        surf = self.objs_surface(dst[1])
        img = self.bitmaps[src[1]]
        region = img[sy:sy + sh, sx:sx + sw]
        if (sw, sh) != (dw, dh) and sw > 0 and sh > 0 and dw > 0 and dh > 0:
            yi = (np.arange(dh) * sh // dh)
            xi = (np.arange(dw) * sw // dw)
            region = region[yi][:, xi]
        _paste(surf.pix, region, dx, dy)
        return 1

    # ── WINMM ────────────────────────────────────────────────────────────────
    def api_timeGetTime(self, a): return self.now
    def api_joyGetNumDevs(self, a): return 0
    def api_joyGetPosEx(self, a): return 0xA7
    def api_joySetThreshold(self, a): return 0
    def api_joySetCapture(self, a): return 0
    def api_joyGetDevCapsA(self, a): return 0

    def api_mmioOpenA(self, a):
        name = self.cstr(a[0])
        data = self.read_file(name)
        if data is None:
            return 0
        h = self.handle("mmio")
        self.mmio[h] = [data, 0, name]
        self.last_wave = name
        return h

    def api_mmioClose(self, a):
        self.mmio.pop(a[0], None); return 0

    def api_mmioRead(self, a):
        st = self.mmio[a[0]]
        n = a.i(2)
        chunk = st[0][st[1]:st[1] + n]
        self.write(a[1], chunk)
        st[1] += len(chunk)
        return len(chunk)

    def api_mmioDescend(self, a):
        st = self.mmio[a[0]]
        data = st[0]
        ck, parent, flags = a[1], a[2], a[3]
        end = len(data)
        if parent:
            pofs = self.r32(parent + 12)
            psz = self.r32(parent + 4)
            end = min(end, pofs + psz)
        want_id = self.r32(ck) if flags & 0x10 else None
        want_type = self.r32(ck + 8) if flags & 0x20 else None
        pos = st[1]
        while pos + 8 <= end:
            cid, csz = struct.unpack_from("<II", data, pos)
            ctype = struct.unpack_from("<I", data, pos + 8)[0] if pos + 12 <= len(data) else 0
            ok = True
            if want_type is not None:
                ok = cid == 0x46464952 and ctype == want_type
            elif want_id is not None:
                ok = cid == want_id
            if ok:
                has_type = cid in (0x46464952, 0x5453494C)
                dofs = pos + 8
                self.write(ck, struct.pack("<IIIII", cid, csz, ctype if has_type else 0, dofs, 0))
                st[1] = dofs + (4 if has_type else 0)
                return 0
            pos += 8 + csz + (csz & 1)
        return 0x0101        # MMIOERR_CHUNKNOTFOUND

    def api_mmioAscend(self, a):
        st = self.mmio[a[0]]
        dofs = self.r32(a[1] + 12)
        sz = self.r32(a[1] + 4)
        st[1] = dofs + sz + (sz & 1)
        return 0

    # ── DirectDraw / DirectSound creation ────────────────────────────────────
    def api_DirectDrawCreate(self, a):
        self.w32(a[1], self.com_object("dd", "ddraw")); return 0
    def api_ord1(self, a):          # DSOUND!DirectSoundCreate
        self.w32(a[1], self.com_object("ds", "dsound")); return 0

    # ── misc ─────────────────────────────────────────────────────────────────
    def api_GetOpenFileNameA(self, a):
        # Harness only: same explicit fixture and capacity rule as native HLE.
        path = os.environ.get("LF2_REPLAY_FILE", "").encode("ascii")
        if not path:
            return 0
        file, cap = self.r32(a[0] + 0x1c), self.r32(a[0] + 0x20)
        if not file or len(path) + 1 > cap:
            return 0
        self.write(file, path + b"\0")
        return 1
    def api_ShellExecuteA(self, a): return 0
    def api_CoInitialize(self, a): return 0
    # DirectShow (music). CLSID_FilterGraph -> IGraphBuilder; QueryInterface
    # hands out the other interfaces by IID. Playback itself is only logged.
    IID_KIND = {"56a868a9": "graph", "56a868b1": "mctrl", "56a868b6": "mevent",
                "56a868b2": "mpos", "56a868b3": "baudio"}

    def _iid_kind(self, p):
        return self.IID_KIND.get("%08x" % self.r32(p))

    def api_CoCreateInstance(self, a):
        kind = self._iid_kind(a[3])
        if kind != "graph":
            return E_FAIL
        self.w32(a[4], self.com_object("graph", "graph"))
        return 0

    def _dshow_qi(self, a):
        kind = self._iid_kind(a[1])
        if kind is None:
            self.w32(a[2], 0)
            return 0x80004002
        self.w32(a[2], self.com_object(kind, kind))
        return 0
    graph_QueryInterface = mctrl_QueryInterface = mevent_QueryInterface = \
        mpos_QueryInterface = baudio_QueryInterface = _dshow_qi

    def graph_RenderFile(self, a):
        n = 0
        chars = []
        while True:
            c = self.r16(a[1] + 2 * n)
            if c == 0:
                break
            chars.append(chr(c)); n += 1
        self.music_file = "".join(chars)
        return 0

    def mctrl_Run(self, a):
        self.music.append((self.frame, "run", getattr(self, "music_file", "")))
        return 0

    def mctrl_Stop(self, a):
        self.music.append((self.frame, "stop", getattr(self, "music_file", "")))
        return 0

    def mevent_GetEvent(self, a):
        return 0x80004004                # E_ABORT: no event pending

    def baudio_put_Volume(self, a):
        self.music.append((self.frame, "volume", a.i(1)))
        return 0
    def api_InternetOpenA(self, a): return 0
    def api_InternetOpenUrlA(self, a): return 0
    def api_InternetReadFile(self, a): return 0
    def api_InternetCloseHandle(self, a): return 1
    def api_WSAStartup(self, a): return 10091   # WSASYSNOTREADY
    def api_WSACleanup(self, a): return 0
    def api_gethostname(self, a): return 0xFFFFFFFF
    def api_gethostbyname(self, a): return 0
    def api_socket(self, a): return 0xFFFFFFFF
    def api_closesocket(self, a): return 0
    def api_htons(self, a): return ((a[0] & 0xFF) << 8) | ((a[0] >> 8) & 0xFF)
    def api_send(self, a): return 0xFFFFFFFF
    def api_recv(self, a): return 0xFFFFFFFF
    def api_sendto(self, a): return 0xFFFFFFFF
    def api_WSAAsyncSelect(self, a): return 0xFFFFFFFF
    def api_ioctlsocket(self, a): return 0xFFFFFFFF
    def api_bind(self, a): return 0xFFFFFFFF
    def api_listen(self, a): return 0xFFFFFFFF
    def api_connect(self, a): return 0xFFFFFFFF
    def api_accept(self, a): return 0xFFFFFFFF
    def api_inet_addr(self, a): return 0xFFFFFFFF
    def api_inet_ntoa(self, a): return 0
    def api_gethostbyaddr(self, a): return 0

    # ── MSVCR80 ──────────────────────────────────────────────────────────────
    def crt_srand(self, a): self.rand_state = a[0]
    def crt_rand(self, a):
        self.rand_state = u32(self.rand_state * 214013 + 2531011)
        return (self.rand_state >> 16) & 0x7FFF
    def crt__controlfp_s(self, a): return 0
    def crt__decode_pointer(self, a): return a[0]
    def crt__encode_pointer(self, a): return a[0]
    def crt__onexit(self, a): return a[0]
    def crt___dllonexit(self, a): return a[0]
    def crt__lock(self, a): return 0
    def crt__unlock(self, a): return 0
    def crt__crt_debugger_hook(self, a): return 0
    def crt___set_app_type(self, a): return 0
    def crt___setusermatherr(self, a): return 0
    def crt__configthreadlocale(self, a): return 0
    def crt__ismbblead(self, a): return 0
    def crt_exit(self, a): raise StopEmu()
    def crt__exit(self, a): raise StopEmu()
    def crt__cexit(self, a): return 0
    def crt__amsg_exit(self, a): raise StopEmu()
    def crt__invoke_watson(self, a): raise RuntimeError("_invoke_watson")
    # Game heap: exact-size LIFO free lists over the bump allocator. Same
    # algorithm as rt::gameAlloc in src/recomp/rt_core.cpp (the diff needs it).
    def game_alloc(self, n, zero):
        size = (max(n, 1) + 15) & ~15
        lst = self.free_lists.get(size)
        if lst:
            p = lst.pop()
            if zero:
                self.write(p, b"\0" * size)
        else:
            p = self.alloc(size)
        self.block_size[p] = size
        return p
    def crt_malloc(self, a): return self.game_alloc(a[0], False)
    def crt_calloc(self, a): return self.game_alloc(a[0] * a[1], True)
    def crt_free(self, a):
        size = self.block_size.pop(a[0], None)
        if size:
            self.free_lists.setdefault(size, []).append(a[0])
        return 0
    def crt_memset(self, a):
        n = a[2]
        if n:
            self.write(a[0], bytes([a[1] & 0xFF]) * n)
        return a[0]
    def crt_memcpy(self, a):
        if a[2]:
            self.write(a[0], self.read(a[1], a[2]))
        return a[0]
    def crt_sprintf(self, a):
        s = c_format(self, self.cstr(a[1]), a, 2)
        self.wstr(a[0], s)
        return len(s)
    def crt_fprintf(self, a):
        f = self.files.get(a[0])
        s = c_format(self, self.cstr(a[1]), a, 2).encode("latin1")
        if f is None:
            return 0xFFFFFFFF
        n = len(s)
        if f.text:
            s = s.replace(b"\n", b"\r\n")     # text-mode write, as MSVCRT
        f.data[f.pos:f.pos + len(s)] = s
        f.pos += len(s)
        return n
    def crt_sscanf(self, a):
        src = ScanSource(self.cstr(a[0]).encode("latin1"))
        n, _ = c_scan(self, src, self.cstr(a[1]), a, 2)
        return n
    def crt_fscanf(self, a):
        f = self.files[a[0]]
        src = ScanSource(bytes(f.data), f.pos)
        n, eof = c_scan(self, src, self.cstr(a[1]), a, 2)
        f.pos = src.pos
        if eof:
            f.eof = True
        return n
    def crt_fgets(self, a):
        f = self.files[a[2]]
        n = a.i(1)
        if f.pos >= len(f.data):
            f.eof = True
            return 0
        end = f.data.find(b"\n", f.pos)
        end = len(f.data) if end < 0 else end + 1
        end = min(end, f.pos + n - 1)
        chunk = bytes(f.data[f.pos:end])
        f.pos = end
        if f.pos >= len(f.data) and not chunk.endswith(b"\n"):
            f.eof = True
        self.write(a[0], chunk + b"\0")
        return a[0]
    def crt_feof(self, a):
        f = self.files.get(a[0])
        return 1 if f is None or f.eof else 0
    def crt_fopen(self, a):
        name = self.cstr(a[0])
        mode = self.cstr(a[1])
        if "w" in mode:
            f = CFile(name, b"", mode)
        else:
            data = self.read_file(name)
            if data is None:
                return 0
            f = CFile(name, data, mode)
        fp = self.alloc(32)
        self.files[fp] = f
        return fp
    def crt_fclose(self, a):
        f = self.files.pop(a[0], None)
        if f and f.writing:
            data = bytes(f.data)
            self.vfs[self.game_rel(f.path).replace("/", "\\").lower()] = data
        return 0
    def crt__chdir(self, a): return 0
    def crt__getcwd(self, a):
        buf = a[0]
        if not buf:     # MSVCR80: malloc(max(size, len+1))
            buf = self.game_alloc(max(a[1], 7), False)
        self.wstr(buf, "C:\\LF2")
        return buf
    def crt__time64(self, a):
        t = 1246878720          # 2009-07-06
        if a[0]:
            self.write(a[0], struct.pack("<q", t))
        return (t, 0)
    def crt__localtime64(self, a):
        if not hasattr(self, "_tm"):
            self._tm = self.alloc(36)
        self.write(self._tm, struct.pack("<9i", 0, 12, 11, 6, 6, 109, 1, 186, 0))
        return self._tm
    def crt___getmainargs(self, a): return 0
    def crt___p__fmode(self, a):
        if not hasattr(self, "_fmode"):
            self._fmode = self.alloc(4)
        return self._fmode
    def crt___p__commode(self, a): return self.crt___p__fmode(a)
    def crt__adjust_fdiv(self, a): return 0

    # ── IDirectDraw ──────────────────────────────────────────────────────────
    def objs_surface(self, obj):
        s = self.objs.get(obj)
        if not isinstance(s, Surface):
            raise RuntimeError("not a surface %x" % obj)
        return s

    def dd_CreateClipper(self, a):
        self.w32(a[2], self.com_object("clip", "clipper")); return 0

    def dd_CreateSurface(self, a):
        desc = a[1]
        flags = self.r32(desc + 4)
        h = self.r32(desc + 8) if flags & 2 else SCREEN_H
        w = self.r32(desc + 12) if flags & 4 else SCREEN_W
        caps = self.r32(desc + 0x68)
        s = Surface(w, h, caps)
        if caps & 0x200:
            self.primary = s
        obj = self.com_object("surf", s)
        self.surfaces[obj] = s
        self.w32(a[2], obj)
        if caps & 0x40 and w == SCREEN_W and h == SCREEN_H and self.back is None:
            self.back = s
        return 0

    def dd_GetDisplayMode(self, a):
        self._fill_desc(a[1], Surface(SCREEN_W, SCREEN_H, 0)); return 0

    # ── IDirectDrawSurface ───────────────────────────────────────────────────
    def _fill_desc(self, p, s, mem=0):
        self.write(p, b"\0" * 0x6C)
        self.write(p, struct.pack("<IIIIi", 0x6C, 0x100F, s.h, s.w, s.w * 4))
        self.w32(p + 0x24, mem)
        self._fill_pf(p + 0x48)
        self.w32(p + 0x68, s.caps)

    def _fill_pf(self, p):
        self.write(p, struct.pack("<8I", 0x20, 0x40, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0))

    def surf_GetPixelFormat(self, a):
        self._fill_pf(a[1]); return 0

    def surf_GetSurfaceDesc(self, a):
        self._fill_desc(a[1], self.objs_surface(a[0])); return 0

    def surf_SetColorKey(self, a):
        s = self.objs_surface(a[0])
        if a[2]:
            lo, hi = struct.unpack("<II", self.read(a[2], 8))
            s.ck = (lo, hi)
        else:
            s.ck = None
        return 0

    def surf_GetDC(self, a):
        h = self.handle("sdc")
        self.dcs[h] = ("surf", a[0])
        self.w32(a[1], h)
        return 0

    def surf_ReleaseDC(self, a):
        self.dcs.pop(a[1], None); return 0

    def surf_IsLost(self, a): return 0
    def surf_Restore(self, a): return 0

    def surf_Lock(self, a):
        s = self.objs_surface(a[0])
        if s.lock_mem is None:
            s.lock_mem = self.alloc(s.w * s.h * 4)
        self.write(s.lock_mem, s.pix.tobytes())
        self._fill_desc(a[2], s, s.lock_mem)
        return 0

    def surf_Unlock(self, a):
        s = self.objs_surface(a[0])
        if s.lock_mem is not None:
            s.pix = np.frombuffer(self.read(s.lock_mem, s.w * s.h * 4),
                                  dtype=np.uint32).reshape(s.h, s.w).copy()
        return 0

    def surf_Flip(self, a):
        self.on_present(); return 0

    def _rect(self, p, s):
        if not p:
            return 0, 0, s.w, s.h
        return struct.unpack("<4i", self.read(p, 16))

    def surf_Blt(self, a):
        dst = self.objs_surface(a[0])
        flags = a[4]
        fx = a[5]
        dl, dt, dr, db = self._rect(a[1], dst)
        if flags & 0x400:                   # COLORFILL
            color = self.r32(fx + 0x50) & 0xFFFFFF
            l, t, r, b = max(dl, 0), max(dt, 0), min(dr, dst.w), min(db, dst.h)
            if r > l and b > t:
                dst.pix[t:b, l:r] = color
            return 0
        if not a[2]:
            return 0
        src = self.objs_surface(a[2])
        sl, st, sr, sb = self._rect(a[3], src)
        mirror = (flags & 0x800) and fx and (self.r32(fx + 4) & 2)
        flipv = (flags & 0x800) and fx and (self.r32(fx + 4) & 4)
        key = src.ck if flags & 0x8000 else None
        self._blit(dst, (dl, dt, dr, db), src, (sl, st, sr, sb), key, mirror, flipv)
        if dst is self.primary:
            self.on_present()
        return 0

    def surf_BltFast(self, a):
        dst = self.objs_surface(a[0])
        x, y = a.i(1), a.i(2)
        src = self.objs_surface(a[3])
        sl, st, sr, sb = self._rect(a[4], src)
        key = src.ck if a[5] & 1 else None
        self._blit(dst, (x, y, x + sr - sl, y + sb - st), src, (sl, st, sr, sb), key, 0, 0)
        return 0

    def _blit(self, dst, drect, src, srect, key, mirror, flipv):
        dl, dt, dr, db = drect
        sl, st, sr, sb = srect
        dw, dh, sw, sh = dr - dl, db - dt, sr - sl, sb - st
        if dw <= 0 or dh <= 0 or sw <= 0 or sh <= 0:
            return
        sl, st = max(sl, 0), max(st, 0)
        region = src.pix[st:st + sh, sl:sl + sw]
        if region.shape != (sh, sw):
            return
        if (sw, sh) != (dw, dh):
            yi = np.arange(dh) * sh // dh
            xi = np.arange(dw) * sw // dw
            region = region[yi][:, xi]
        if mirror:
            region = region[:, ::-1]
        if flipv:
            region = region[::-1]
        mask = None
        if key is not None:
            lo, hi = key
            mask = (region < lo) | (region > hi)
        _paste(dst.pix, region, dl, dt, mask)

    def on_present(self):
        pass

    # ── IDirectSound ─────────────────────────────────────────────────────────
    def ds_CreateSoundBuffer(self, a):
        desc = a[1]
        size = self.r32(desc + 8)
        b = SoundBuf(self.last_wave, size, None)
        obj = self.com_object("dsb", b)
        self.dsbufs[obj] = b
        self.w32(a[2], obj)
        return 0

    def ds_DuplicateSoundBuffer(self, a):
        orig = self.objs[a[1]]
        b = SoundBuf(orig.name, orig.size, None)
        b.mem = orig.mem
        obj = self.com_object("dsb", b)
        self.dsbufs[obj] = b
        self.w32(a[2], obj)
        return 0

    def dsb_Lock(self, a):
        b = self.objs[a[0]]
        if b.mem is None:
            b.mem = self.alloc(max(b.size, 16))
        self.w32(a[3], b.mem)
        self.w32(a[4], a[2] or b.size)
        if a[5]:
            self.w32(a[5], 0)
        if a[6]:
            self.w32(a[6], 0)
        return 0

    def dsb_Play(self, a):
        b = self.objs[a[0]]
        self.sounds.append((self.frame, b.name, getattr(b, "vol", 0), getattr(b, "pan", 0)))
        return 0

    def dsb_SetVolume(self, a):
        self.objs[a[0]].vol = a.i(1); return 0

    def dsb_SetPan(self, a):
        self.objs[a[0]].pan = a.i(1); return 0

    def dsb_GetStatus(self, a):
        self.w32(a[1], 0); return 0


def _paste(dst, region, x, y, mask=None):
    h, w = region.shape
    H, W = dst.shape
    x0, y0 = max(x, 0), max(y, 0)
    x1, y1 = min(x + w, W), min(y + h, H)
    if x1 <= x0 or y1 <= y0:
        return
    r = region[y0 - y:y1 - y, x0 - x:x1 - x]
    if mask is None:
        dst[y0:y1, x0:x1] = r
    else:
        m = mask[y0 - y:y1 - y, x0 - x:x1 - x]
        view = dst[y0:y1, x0:x1]
        view[m] = r[m]
