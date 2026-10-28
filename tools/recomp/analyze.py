"""Control-flow recovery for lf2.exe (static recompilation, stage 1).

Recursive descent from every known entry point. Produces, per function, the
set of instructions reachable from its entry *without leaving it through a
call*, the jump-table targets, and the list of indirect branch sites that
could not be resolved statically (those must not exist for a clean build).
"""
from __future__ import annotations

import collections
import hashlib
import json
import pathlib
import struct

import capstone
from capstone import x86_const as XC
import pefile

ROOT = pathlib.Path(__file__).resolve().parents[2]
import os
EXE = pathlib.Path(os.environ.get("LF2_EXE", ROOT / "reference/decomp/lf2.exe"))
SHA = "12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5"


class Image:
    def __init__(self, path=EXE):
        raw = pathlib.Path(path).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == SHA, "lf2.exe v2.0a expected"
        self.pe = pe = pefile.PE(data=raw)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.mem = pe.get_memory_mapped_image()
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        self.sections = []
        for s in pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            lo = self.base + s.VirtualAddress
            self.sections.append((name, lo, lo + max(s.Misc_VirtualSize, s.SizeOfRawData),
                                  s.Characteristics))
        text = [s for s in self.sections if s[0] == ".text"][0]
        self.text_lo, self.text_hi = text[1], text[2]
        self.imports = {}           # IAT slot address -> "dll!name"
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode().lower()
            for imp in entry.imports:
                name = imp.name.decode() if imp.name else "#%d" % imp.ordinal
                self.imports[imp.address] = dll + "!" + name
        self.entry = self.base + pe.OPTIONAL_HEADER.AddressOfEntryPoint

    def in_text(self, a):
        return self.text_lo <= a < self.text_hi

    def u32(self, a):
        o = a - self.base
        return struct.unpack_from("<I", self.mem, o)[0]

    def u8(self, a):
        return self.mem[a - self.base]

    def bytes_at(self, a, n):
        o = a - self.base
        return self.mem[o:o + n]


class Disasm:
    def __init__(self, img: Image):
        self.img = img
        self.cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.cs.detail = True
        self.cache = {}

    def at(self, a):
        ins = self.cache.get(a)
        if ins is None:
            code = self.img.bytes_at(a, 16)
            it = self.cs.disasm(bytes(code), a, 1)
            ins = next(it, None)
            if ins is None:
                raise ValueError("cannot decode at %08x" % a)
            self.cache[a] = ins
        return ins


BRANCH_COND = {XC.X86_INS_JA, XC.X86_INS_JAE, XC.X86_INS_JB, XC.X86_INS_JBE,
               XC.X86_INS_JE, XC.X86_INS_JNE, XC.X86_INS_JG, XC.X86_INS_JGE,
               XC.X86_INS_JL, XC.X86_INS_JLE, XC.X86_INS_JO, XC.X86_INS_JNO,
               XC.X86_INS_JP, XC.X86_INS_JNP, XC.X86_INS_JS, XC.X86_INS_JNS,
               XC.X86_INS_JCXZ, XC.X86_INS_JECXZ}


def a_lo(ins):
    return ins.address


def op_imm(ins, i):
    op = ins.operands[i]
    return op.imm if op.type == XC.X86_OP_IMM else None


class Function:
    def __init__(self, entry):
        self.entry = entry
        self.insns = {}             # addr -> capstone insn
        self.labels = set()         # branch targets inside the function
        self.jumptables = {}        # jmp addr -> list of targets
        self.calls = set()          # direct call targets
        self.tailjumps = {}         # jmp addr -> target function entry
        self.unresolved = []        # indirect jmp sites we could not resolve
        self.indirect_calls = []


class Program:
    def __init__(self, img: Image):
        self.img = img
        self.d = Disasm(img)
        self.funcs: dict[int, Function] = {}
        self.entries = set()
        self.data_code_refs = set()

    # ── jump tables ──────────────────────────────────────────────────────────
    def jumptable(self, fn: Function, ins, prev):
        """MSVC switch lowering:
             cmp r, N ; ja default ; [movzx r, byte [r+btab]] ; jmp [r*4+tab]
        Returns targets (by table index) or None."""
        m = ins.operands[0]
        if m.type != XC.X86_OP_MEM or m.mem.scale != 4 or m.mem.base != 0:
            return None
        tab = m.mem.disp & 0xFFFFFFFF
        idx_reg = m.mem.index
        bound = None
        btab = None
        # walk back through the linear predecessors in this block
        for p in reversed(prev[-8:]):
            if p.id == XC.X86_INS_MOVZX and p.operands[0].type == XC.X86_OP_REG \
                    and p.operands[0].reg == idx_reg and p.operands[1].type == XC.X86_OP_MEM:
                mm = p.operands[1].mem
                btab = (mm.disp & 0xFFFFFFFF, mm.base)
                idx_reg = mm.base
                continue
            if p.id == XC.X86_INS_CMP and p.operands[0].type == XC.X86_OP_REG \
                    and p.operands[1].type == XC.X86_OP_IMM and p.operands[0].reg == idx_reg:
                bound = p.operands[1].imm
                break
        if bound is None:
            # the bound check may sit in another block (jmp L; L: jmp [r*4+t])
            for pa in sorted((x for x in fn.insns if a_lo(ins) - 0x80 <= x < ins.address),
                             reverse=True):
                p = fn.insns[pa]
                if p.id == XC.X86_INS_CMP and p.operands[0].type == XC.X86_OP_REG \
                        and p.operands[1].type == XC.X86_OP_IMM and p.operands[0].reg == idx_reg:
                    bound = p.operands[1].imm
                    break
        if bound is None:
            return None
        if btab:
            nidx = max(self.img.u8(btab[0] + i) for i in range(bound + 1)) + 1
        else:
            nidx = bound + 1
        targets = [self.img.u32(tab + 4 * i) for i in range(nidx)]
        if not all(self.img.in_text(t) for t in targets):
            return None
        return {"table": tab, "targets": targets, "btab": btab[0] if btab else None,
                "bound": bound}

    # ── recursive descent ────────────────────────────────────────────────────
    def explore(self, entry):
        if entry in self.funcs:
            return self.funcs[entry]
        fn = Function(entry)
        self.funcs[entry] = fn
        work = [entry]
        fn.labels.add(entry)
        while work:
            a = work.pop()
            prev = []
            while True:
                if a in fn.insns:
                    fn.labels.add(a)
                    break
                if a != entry and a in self.entries and a in self.funcs and a != fn.entry:
                    # falls into another function: tail call
                    fn.tailjumps[a] = a
                    break
                ins = self.d.at(a)
                fn.insns[a] = ins
                nxt = a + ins.size
                iid = ins.id
                if iid == XC.X86_INS_RET or iid == XC.X86_INS_RETF:
                    break
                if iid == XC.X86_INS_JMP:
                    t = op_imm(ins, 0)
                    if t is not None:
                        if t in self.entries and t != entry:
                            fn.tailjumps[a] = t
                        else:
                            fn.labels.add(t)
                            work.append(t)
                    elif self.iat_target(ins) is not None:
                        fn.tailjumps[a] = ("import", self.iat_target(ins))
                    else:
                        jt = self.jumptable(fn, ins, prev)
                        if jt:
                            fn.jumptables[a] = jt
                            for t in set(jt["targets"]):
                                fn.labels.add(t)
                                work.append(t)
                        else:
                            fn.unresolved.append(a)
                    break
                if iid in BRANCH_COND or iid in (XC.X86_INS_LOOP, XC.X86_INS_LOOPE,
                                                  XC.X86_INS_LOOPNE):
                    t = op_imm(ins, 0)
                    fn.labels.add(t)
                    fn.labels.add(nxt)
                    work.append(t)
                    prev.append(ins)
                    a = nxt
                    continue
                if iid == XC.X86_INS_CALL:
                    t = op_imm(ins, 0)
                    if t is not None:
                        fn.calls.add(t)
                        if t in NORETURN:
                            break
                    else:
                        fn.indirect_calls.append(a)
                        if self.is_noreturn_import(ins):
                            break
                if iid == XC.X86_INS_INT3 or iid == XC.X86_INS_HLT:
                    break
                prev.append(ins)
                a = nxt
        return fn

    def iat_target(self, ins):
        op = ins.operands[0]
        if op.type == XC.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            slot = op.mem.disp & 0xFFFFFFFF
            if slot in self.img.imports:
                return slot
        return None

    def is_noreturn_import(self, ins):
        op = ins.operands[0]
        if op.type == XC.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            name = self.img.imports.get(op.mem.disp & 0xFFFFFFFF, "")
            return name.split("!")[-1] in ("exit", "_exit", "_amsg_exit", "_invoke_watson",
                                            "?terminate@@YAXXZ", "__CxxThrowException@8")
        return False

    def build(self, seeds):
        self.entries = set(seeds)
        todo = list(seeds)
        done = set()
        while todo:
            e = todo.pop()
            if e in done:
                continue
            done.add(e)
            fn = self.explore(e)
            for t in list(fn.calls) + [v for v in fn.tailjumps.values() if isinstance(v, int)]:
                if t not in self.entries:
                    self.entries.add(t)
                if t not in done:
                    todo.append(t)
        # a function's body might have swallowed a later-discovered entry:
        # re-explore everything with the final entry set (stable split).
        changed = True
        while changed:
            changed = False
            self.funcs = {}
            done = set()
            todo = sorted(self.entries)
            for e in todo:
                fn = self.explore(e)
                for t in list(fn.calls) + [v for v in fn.tailjumps.values() if isinstance(v, int)]:
                    if t not in self.entries:
                        self.entries.add(t)
                        changed = True
        return self


# functions that never return (CRT)
NORETURN = set()


def data_function_pointers(img: Image, known):
    """Addresses of known functions stored as data (vtables, callbacks)."""
    refs = set()
    for name, lo, hi, ch in img.sections:
        if name == ".text":
            continue
        for a in range(lo, hi - 3, 4):
            try:
                v = img.u32(a)
            except struct.error:
                break
            if v in known:
                refs.add(v)
    return refs


def immediate_function_refs(prog: Program, known):
    refs = set()
    for fn in prog.funcs.values():
        for ins in fn.insns.values():
            if ins.id in (XC.X86_INS_PUSH, XC.X86_INS_MOV):
                for op in ins.operands:
                    if op.type == XC.X86_OP_IMM and (op.imm & 0xFFFFFFFF) in known:
                        refs.add(op.imm & 0xFFFFFFFF)
    return refs


# Function entry points to start from. The list lives in the repository
# (addresses only), so a fresh clone builds without the Ghidra export that it
# was taken from; LF2_SEEDS=<file> points to another list (one hex address per
# line, '#' comments).
SEEDS = pathlib.Path(os.environ.get("LF2_SEEDS", pathlib.Path(__file__).with_name("lf2_seeds.txt")))


def load_seeds(img: Image):
    seeds = set()
    for ln in SEEDS.read_text().splitlines():
        ln = ln.split("#", 1)[0].strip()
        if ln:
            a = int(ln, 16)
            if img.in_text(a):
                seeds.add(a)
    seeds.add(img.entry)
    return seeds


def main():
    img = Image()
    seeds = load_seeds(img)
    prog = Program(img).build(seeds)
    n_ins = sum(len(f.insns) for f in prog.funcs.values())
    unresolved = [(hex(f.entry), [hex(x) for x in f.unresolved])
                  for f in prog.funcs.values() if f.unresolved]
    icalls = sum(len(f.indirect_calls) for f in prog.funcs.values())
    jts = sum(len(f.jumptables) for f in prog.funcs.values())
    print("functions", len(prog.funcs), "instructions", n_ins, "jumptables", jts,
          "indirect calls", icalls)
    print("unresolved indirect jumps:", unresolved)
    mn = collections.Counter(i.mnemonic for f in prog.funcs.values() for i in f.insns.values())
    print(" ".join("%s:%d" % kv for kv in mn.most_common()))
    known = set(prog.funcs)
    print("function pointers in data:", len(data_function_pointers(img, known)))
    print("function pointers as immediates:", len(immediate_function_refs(prog, known)))
    # overlap check: instruction claimed by two functions
    owner = collections.defaultdict(list)
    for f in prog.funcs.values():
        for a in f.insns:
            owner[a].append(f.entry)
    shared = {a: o for a, o in owner.items() if len(o) > 1}
    print("instructions shared by >1 function:", len(shared))
    covered = set(owner)
    return prog


if __name__ == "__main__":
    main()
