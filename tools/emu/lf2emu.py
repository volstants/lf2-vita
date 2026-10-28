"""Headless execution oracle for Little Fighter 2 v2.0a (lf2.exe).

The original executable is mapped into a private Unicorn x86-32 machine and
run unmodified. Every import (163 symbols in 13 DLLs) is replaced by a Python
implementation: files are served read-only from the extracted game folder,
DirectDraw surfaces are numpy images, DirectSound buffers only record which
sound was played, time is virtual. No Windows API or OS call is executed and
the process is never launched.

What this gives the port:
  * run a single function on synthetic state (function-level differential),
  * run the whole game loop with scripted keys and dump object state per tick
    (the "Nivel A'" trace of ORACULO.md), and
  * the rendered back buffer of the original, to compare pictures.
"""
from __future__ import annotations

import hashlib
import os
import pathlib
import struct
import sys

import numpy as np
import pefile
import unicorn
from unicorn import x86_const as X

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXE = ROOT / "reference/decomp/lf2.exe"
SHA = "12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5"

# Guest address space. MUST match src/recomp/guest_layout.h: the recompiled
# port and this oracle allocate identically, so guest memory can be diffed.
TIB_BASE = 0x00760000
STACK_TOP = 0x00A00000
STACK_SIZE = 0x00200000
HEAP_BASE = 0x00A00000
HEAP_SIZE = 0x08000000        # 128 MB: the game allocates an 81 MB world object
GUEST_END = HEAP_BASE + HEAP_SIZE
STUB_BASE = 0xF0000000        # import/COM stubs: addresses only, never memory
STUB_SIZE = 0x00100000
GDT_BASE = 0xF8000000         # Unicorn-only plumbing
MAGIC_RET = 0xF9000000


def u32(v):
    return v & 0xFFFFFFFF


class StopEmu(Exception):
    pass


class Machine:
    """PE image + memory + import dispatch. Subclasses add the API surface."""

    def __init__(self, game_dir=None):
        raw = EXE.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == SHA, "unexpected lf2.exe"
        self.pe = pe = pefile.PE(data=raw)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        u = self.u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(self.base, size)
        u.mem_write(self.base, pe.get_memory_mapped_image())
        u.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE)
        u.mem_map(HEAP_BASE, HEAP_SIZE)
        u.mem_map(STUB_BASE, STUB_SIZE)
        u.mem_write(STUB_BASE, b"\xC3" * STUB_SIZE)
        u.mem_map(MAGIC_RET & ~0xFFF, 0x1000)
        self._setup_fs()
        # Windows starts every thread with x87 precision control = 53 bits
        # (FCW 0x027F); the recompiled port computes in double for the same
        # reason. (Unicorn's reset value reads back as 0.)
        u.reg_write(X.UC_X86_REG_FPCW, 0x027F)
        self.heap = HEAP_BASE
        self.stub_next = STUB_BASE
        self.stubs = {}           # addr -> (name, handler, nargs|None for cdecl)
        self.log = []
        self.trace_imports = False
        # import table
        self.iat = {}
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode().lower()
            for imp in entry.imports:
                name = imp.name.decode() if imp.name else "#%d" % imp.ordinal
                addr = self.new_stub(dll + "!" + name, None, None)
                self.iat[name] = addr
                u.mem_write(imp.address, struct.pack("<I", addr))
        u.hook_add(unicorn.UC_HOOK_CODE, self._on_stub, begin=STUB_BASE,
                   end=STUB_BASE + STUB_SIZE - 1)
        u.hook_add(unicorn.UC_HOOK_MEM_READ_UNMAPPED | unicorn.UC_HOOK_MEM_WRITE_UNMAPPED
                   | unicorn.UC_HOOK_MEM_FETCH_UNMAPPED, self._on_bad_mem)
        self.bad = None
        u.hook_add(unicorn.UC_HOOK_INSN, self._on_cpuid, None, 1, 0, X.UC_X86_INS_CPUID)

    def _on_cpuid(self, uc, user):
        # Same answer as rc_cpuid() in src/recomp/rt_core.cpp.
        leaf = uc.reg_read(X.UC_X86_REG_EAX)
        if leaf == 0:
            v = (1, 0x756E6547, 0x6C65746E, 0x49656E69)
        elif leaf == 1:
            v = (0x6F6, 0, 0, (1 << 0) | (1 << 4) | (1 << 15) | (1 << 23) | (1 << 25) | (1 << 26))
        else:
            v = (0, 0, 0, 0)
        for r, x in zip((X.UC_X86_REG_EAX, X.UC_X86_REG_EBX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX), v):
            uc.reg_write(r, x)
        return 1          # instruction handled

    # ── x86 plumbing ─────────────────────────────────────────────────────────
    def _setup_fs(self):
        u = self.u
        u.mem_map(TIB_BASE, 0x1000)
        u.mem_map(GDT_BASE, 0x1000)
        # TIB: ExceptionList=-1, StackBase, StackLimit, Self
        u.mem_write(TIB_BASE, struct.pack("<IIII", 0xFFFFFFFF, STACK_TOP,
                                          STACK_TOP - STACK_SIZE, 0))
        u.mem_write(TIB_BASE + 0x18, struct.pack("<I", TIB_BASE))

        def desc(base, limit, access, flags):
            d = limit & 0xFFFF
            d |= (base & 0xFFFFFF) << 16
            d |= access << 40
            d |= ((limit >> 16) & 0xF) << 48
            d |= flags << 52
            d |= ((base >> 24) & 0xFF) << 56
            return struct.pack("<Q", d)
        gdt = b"\0" * 8
        gdt += desc(0, 0xFFFFF, 0x9A, 0xC)        # 1: code
        gdt += desc(0, 0xFFFFF, 0x92, 0xC)        # 2: data
        gdt += desc(TIB_BASE, 0xFFF, 0x92, 0x4)   # 3: fs (byte granular)
        gdt += desc(0, 0xFFFFF, 0x96, 0xC)        # 4: stack (data, expand-up)
        u.mem_write(GDT_BASE, gdt)
        u.reg_write(X.UC_X86_REG_GDTR, (0, GDT_BASE, len(gdt) - 1, 0))
        u.reg_write(X.UC_X86_REG_FS, 3 << 3)
        u.reg_write(X.UC_X86_REG_SS, 2 << 3)
        u.reg_write(X.UC_X86_REG_DS, 2 << 3)
        u.reg_write(X.UC_X86_REG_ES, 2 << 3)
        u.reg_write(X.UC_X86_REG_GS, 2 << 3)

    def _on_bad_mem(self, uc, access, address, size, value, user):
        eip = uc.reg_read(X.UC_X86_REG_EIP)
        self.bad = (access, address, size, eip)
        return False

    def new_stub(self, name, handler, nargs):
        addr = self.stub_next
        self.stub_next += 4
        self.stubs[addr] = [name, handler, nargs]
        return addr

    def bind(self, name, handler, nargs=None):
        """Attach an implementation. nargs=None -> cdecl, else stdcall/thiscall
        callee-cleanup of nargs dwords."""
        addr = self.iat[name]
        self.stubs[addr][1] = handler
        self.stubs[addr][2] = nargs

    def _on_stub(self, uc, addr, size, user):
        ent = self.stubs.get(addr)
        if ent is None:
            return
        name, handler, nargs = ent
        esp = uc.reg_read(X.UC_X86_REG_ESP)
        ret = self.r32(esp)
        if handler is None:
            self.bad = ("unimplemented import", name, ret)
            uc.emu_stop()
            return
        a = _Args(self, esp + 4)
        if self.trace_imports:
            self.log.append(name)
        try:
            res = handler(a)
        except StopEmu:
            uc.emu_stop()
            return
        if res is None:
            res = 0
        if isinstance(res, dict):            # redirect into guest code
            uc.reg_write(X.UC_X86_REG_ESP, res["esp"])
            uc.reg_write(X.UC_X86_REG_EIP, res["eip"])
            return
        if isinstance(res, tuple):          # (eax, edx) for 64-bit returns
            uc.reg_write(X.UC_X86_REG_EDX, u32(res[1]))
            res = res[0]
        if isinstance(res, float):
            raise RuntimeError("float return not supported: " + name)
        uc.reg_write(X.UC_X86_REG_EAX, u32(res))
        uc.reg_write(X.UC_X86_REG_ESP, esp + 4 + 4 * (nargs or 0))
        uc.reg_write(X.UC_X86_REG_EIP, ret)

    # ── memory helpers ───────────────────────────────────────────────────────
    def alloc(self, n, align=16):
        a = (self.heap + align - 1) & ~(align - 1)
        n = max(n, 1)
        if a + n > HEAP_BASE + HEAP_SIZE:
            raise MemoryError("emulated heap exhausted")
        self.heap = a + n
        self.u.mem_write(a, b"\0" * n)
        return a

    def w32(self, a, v): self.u.mem_write(a, struct.pack("<I", u32(v)))
    def w16(self, a, v): self.u.mem_write(a, struct.pack("<H", v & 0xFFFF))
    def w8(self, a, v): self.u.mem_write(a, bytes([v & 0xFF]))
    def r32(self, a): return struct.unpack("<I", self.u.mem_read(a, 4))[0]
    def ri32(self, a): return struct.unpack("<i", self.u.mem_read(a, 4))[0]
    def r16(self, a): return struct.unpack("<H", self.u.mem_read(a, 2))[0]
    def r8(self, a): return self.u.mem_read(a, 1)[0]
    def wf64(self, a, v): self.u.mem_write(a, struct.pack("<d", v))
    def rf64(self, a): return struct.unpack("<d", self.u.mem_read(a, 8))[0]
    def read(self, a, n): return bytes(self.u.mem_read(a, n))
    def write(self, a, b): self.u.mem_write(a, bytes(b))

    def cstr(self, a, limit=4096):
        if a == 0:
            return None
        out = bytearray()
        while len(out) < limit:
            chunk = self.u.mem_read(a + len(out), 64)
            z = chunk.find(0)
            if z >= 0:
                out += chunk[:z]
                break
            out += chunk
        return out.decode("latin1")

    def wstr(self, a, s):
        self.u.mem_write(a, s.encode("latin1") + b"\0")

    # ── calling ──────────────────────────────────────────────────────────────
    def call(self, fn, args=(), ecx=None, max_insn=0, until=None):
        """Call fn(args...) (cdecl/stdcall/thiscall all work: we only need the
        return). Runs until it returns to MAGIC_RET or `until` is hit."""
        u = self.u
        sp = STACK_TOP - 0x1000
        for a in reversed(args):
            sp -= 4
            self.w32(sp, a)
        sp -= 4
        self.w32(sp, MAGIC_RET)
        u.reg_write(X.UC_X86_REG_ESP, sp)
        u.reg_write(X.UC_X86_REG_EBP, 0)
        if ecx is not None:
            u.reg_write(X.UC_X86_REG_ECX, ecx)
        self.bad = None
        try:
            u.emu_start(fn, until if until is not None else MAGIC_RET, count=max_insn)
        except unicorn.UcError as e:
            raise RuntimeError("emulation fault %s at eip=%08x bad=%r" % (
                e, u.reg_read(X.UC_X86_REG_EIP), self.bad))
        if self.bad:
            raise RuntimeError("stopped: %r" % (self.bad,))
        return u.reg_read(X.UC_X86_REG_EAX)


class _Args:
    """Lazy view of stack arguments for an import call."""
    __slots__ = ("m", "sp")

    def __init__(self, m, sp):
        self.m, self.sp = m, sp

    def __getitem__(self, i):
        return self.m.r32(self.sp + 4 * i)

    def i(self, i):
        return self.m.ri32(self.sp + 4 * i)

    def f64(self, i):          # a double occupying slots i, i+1
        return self.m.rf64(self.sp + 4 * i)

    def addr(self, i):
        return self.sp + 4 * i
