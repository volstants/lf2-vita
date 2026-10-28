"""x86-32 -> C translation of lf2.exe (static recompilation, stage 2).

Every recovered function becomes `void f_XXXXXXXX(void)` operating on:
  * guest memory `M` (flat, guest address == offset, little endian),
  * the register file `R` (loaded into locals on entry, stored back around
    every call and on return, so the C compiler keeps registers in machine
    registers inside a function),
  * lazy condition flags in locals (F_op/F_a/F_b/F_r/F_cf/F_of/F_sz),
  * the x87 stack in `R.st[]` at 53-bit (double) precision, which is the
    precision-control Windows gives every thread.
Calling conventions need no special handling: arguments, return addresses
and callee cleanup all live in the guest stack exactly as in the original.
"""
from __future__ import annotations

import os

import pathlib
import sys

from capstone import x86_const as XC

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from analyze import EXE, Image, Program, load_seeds, BRANCH_COND  # noqa: E402

R32 = {XC.X86_REG_EAX: "eax", XC.X86_REG_ECX: "ecx", XC.X86_REG_EDX: "edx",
       XC.X86_REG_EBX: "ebx", XC.X86_REG_ESP: "esp", XC.X86_REG_EBP: "ebp",
       XC.X86_REG_ESI: "esi", XC.X86_REG_EDI: "edi"}
R16 = {XC.X86_REG_AX: "eax", XC.X86_REG_CX: "ecx", XC.X86_REG_DX: "edx",
       XC.X86_REG_BX: "ebx", XC.X86_REG_SP: "esp", XC.X86_REG_BP: "ebp",
       XC.X86_REG_SI: "esi", XC.X86_REG_DI: "edi"}
R8L = {XC.X86_REG_AL: "eax", XC.X86_REG_CL: "ecx", XC.X86_REG_DL: "edx",
       XC.X86_REG_BL: "ebx"}
R8H = {XC.X86_REG_AH: "eax", XC.X86_REG_CH: "ecx", XC.X86_REG_DH: "edx",
       XC.X86_REG_BH: "ebx"}
SEGS = {XC.X86_REG_CS: 0x1B, XC.X86_REG_SS: 0x23, XC.X86_REG_DS: 0x23,
        XC.X86_REG_ES: 0x23, XC.X86_REG_FS: 0x3B, XC.X86_REG_GS: 0x00}
FPR = {getattr(XC, "X86_REG_ST%d" % i): i for i in range(8)}

REGS = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
SYNC_OUT = " ".join("R.%s=%s;" % (r, r) for r in REGS)
SYNC_IN = " ".join("%s=R.%s;" % (r, r) for r in REGS)
# LF2_RC_TRACE=1 at generation time: call rc_trace() before every instruction
# (register snapshot) so a run can be diffed against the Unicorn oracle.
TRACE = os.environ.get("LF2_RC_TRACE") == "1"
# LF2_HOOKS=1: every translated function first checks rc_hook[i] (i = its index in
# rc_fn_table) and, when set, runs that native replacement instead. Used by the
# decompilation harness; off by default so the normal output stays unchanged.
HOOKS = os.environ.get("LF2_HOOKS") == "1"

# Functions implemented natively instead of translated (CRT helpers whose
# body uses instructions we do not model, e.g. SSE2 conversion).
NATIVE = {
    0x4450D0: "n_ftol2_sse",
    0x4148A0: "n_dat_decrypt",  # .dat decryptor (win32_api.cpp): same CRT effects, ~100x faster
}
# The decompilation harness keeps the decryptor translated, so its decompiled
# version (a hook) runs on every .dat the suite loads.
if HOOKS:
    del NATIVE[0x4148A0]
# Functions still translated, but every call goes through a native wrapper
# (w_XXXXXXXX) that may run host code before/after the original.
WRAP = {
    0x43E9A0: "w_0043e9a0",     # per-frame routine called by WinMain's loop
}

JCC = {
    XC.X86_INS_JE: "ZF", XC.X86_INS_JNE: "!ZF",
    XC.X86_INS_JL: "(SF!=OF)", XC.X86_INS_JGE: "(SF==OF)",
    XC.X86_INS_JLE: "(ZF||SF!=OF)", XC.X86_INS_JG: "(!ZF&&SF==OF)",
    XC.X86_INS_JB: "CF", XC.X86_INS_JAE: "!CF",
    XC.X86_INS_JBE: "(CF||ZF)", XC.X86_INS_JA: "(!CF&&!ZF)",
    XC.X86_INS_JS: "SF", XC.X86_INS_JNS: "!SF",
    XC.X86_INS_JO: "OF", XC.X86_INS_JNO: "!OF",
    XC.X86_INS_JP: "PF", XC.X86_INS_JNP: "!PF",
}
SETCC = {
    XC.X86_INS_SETE: "ZF", XC.X86_INS_SETNE: "!ZF",
    XC.X86_INS_SETL: "(SF!=OF)", XC.X86_INS_SETGE: "(SF==OF)",
    XC.X86_INS_SETLE: "(ZF||SF!=OF)", XC.X86_INS_SETG: "(!ZF&&SF==OF)",
    XC.X86_INS_SETB: "CF", XC.X86_INS_SETAE: "!CF",
    XC.X86_INS_SETBE: "(CF||ZF)", XC.X86_INS_SETA: "(!CF&&!ZF)",
    XC.X86_INS_SETS: "SF", XC.X86_INS_SETNS: "!SF",
    XC.X86_INS_SETO: "OF", XC.X86_INS_SETNO: "!OF",
    XC.X86_INS_SETP: "PF", XC.X86_INS_SETNP: "!PF",
}
COND = {"ZF": "FL_ZF", "SF": "FL_SF", "CF": "FL_CF", "OF": "FL_OF", "PF": "FL_PF"}


def cond_expr(c):
    out = c
    for k, v in COND.items():
        out = out.replace(k, v)
    return out


class Unsupported(Exception):
    pass


class FnEmitter:
    def __init__(self, prog: Program, fn, entries):
        self.prog = prog
        self.img = prog.img
        self.fn = fn
        self.entries = entries
        self.out = []
        self.used_labels = set()

    # ── operands ─────────────────────────────────────────────────────────────
    def addr(self, op):
        m = op.mem
        parts = []
        if m.base:
            parts.append(self.reg32(m.base))
        if m.index:
            idx = self.reg32(m.index)
            parts.append(idx if m.scale == 1 else "%s*%d" % (idx, m.scale))
        disp = m.disp & 0xFFFFFFFF
        if disp or not parts:
            parts.append("0x%xu" % disp)
        e = "+".join(parts)
        if m.segment == XC.X86_REG_FS:
            e = "R.fs_base+" + e
        elif m.segment not in (0, XC.X86_REG_DS, XC.X86_REG_SS, XC.X86_REG_ES):
            raise Unsupported("segment")
        return "(uint32_t)(%s)" % e

    def reg32(self, r):
        if r in R32:
            return R32[r]
        raise Unsupported("reg %d" % r)

    def read(self, op, size=None):
        size = size or op.size
        if op.type == XC.X86_OP_REG:
            r = op.reg
            if r in R32:
                return R32[r]
            if r in R16:
                return "(%s&0xffffu)" % R16[r]
            if r in R8L:
                return "(%s&0xffu)" % R8L[r]
            if r in R8H:
                return "((%s>>8)&0xffu)" % R8H[r]
            if r in SEGS:
                return "0x%xu" % SEGS[r]
            raise Unsupported("read reg %d" % r)
        if op.type == XC.X86_OP_IMM:
            mask = {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}[size]
            return "0x%xu" % (op.imm & mask)
        if op.type == XC.X86_OP_MEM:
            return "RD%d(%s)" % (size * 8, self.addr(op))
        raise Unsupported("operand")

    def write(self, op, expr, size=None):
        size = size or op.size
        if op.type == XC.X86_OP_REG:
            r = op.reg
            if r in R32:
                return "%s=(uint32_t)(%s);" % (R32[r], expr)
            if r in R16:
                n = R16[r]
                return "%s=(%s&0xffff0000u)|((uint32_t)(%s)&0xffffu);" % (n, n, expr)
            if r in R8L:
                n = R8L[r]
                return "%s=(%s&0xffffff00u)|((uint32_t)(%s)&0xffu);" % (n, n, expr)
            if r in R8H:
                n = R8H[r]
                return "%s=(%s&0xffff00ffu)|(((uint32_t)(%s)&0xffu)<<8);" % (n, n, expr)
            raise Unsupported("write reg")
        if op.type == XC.X86_OP_MEM:
            return "WR%d(%s,%s);" % (size * 8, self.addr(op), expr)
        raise Unsupported("write operand")

    def emit(self, s):
        self.out.append("  " + s)

    def goto(self, target):
        if target in self.fn.insns:
            self.used_labels.add(target)
            return "goto L_%08x;" % target
        # jump to another function's code (shared tail or tail call)
        return self.tail(target)

    def tail(self, target):
        return "{%s %s(); return;}" % (SYNC_OUT, self.fname(target))

    def fname(self, a):
        if a in NATIVE:
            return NATIVE[a]
        if a in WRAP:
            return WRAP[a]
        return "f_%08x" % a

    # ── flags ────────────────────────────────────────────────────────────────
    def flags(self, op, a, b, r, sz, cf="0", of="0"):
        self.emit("F_op=%s; F_a=(uint32_t)(%s); F_b=(uint32_t)(%s); F_r=(uint32_t)(%s); "
                  "F_sz=%d; F_cf=%s; F_of=%s;" % (op, a, b, r, sz, cf, of))

    # ── main translation ─────────────────────────────────────────────────────
    def body(self):
        fn = self.fn
        addrs = sorted(fn.insns)
        # the entry is not always the lowest address (e.g. "mov ecx,obj; jmp
        # shared" thunks whose target sits earlier in .text): start there.
        if addrs[0] != fn.entry:
            self.emit(self.goto(fn.entry))
        for i, a in enumerate(addrs):
            ins = fn.insns[a]
            self.out.append("L_%08x:" % a)
            self.cur = ins
            if TRACE:
                self.emit("if (rc_tracing) rc_trace(0x%xu,%s);" % (a, ",".join(REGS)))
            self.translate(ins)
            nxt = a + ins.size
            # fall-through into code not owned by this function
            if not self.ends_flow(ins):
                if nxt not in fn.insns:
                    self.emit(self.tail(nxt))
                elif i + 1 < len(addrs) and addrs[i + 1] != nxt:
                    self.emit(self.goto(nxt))
        lines = []
        for l in self.out:
            if l.startswith("L_"):
                a = int(l[2:10], 16)
                if a in self.used_labels:
                    lines.append(l + " ;")
            else:
                lines.append(l)
        return lines

    def ends_flow(self, ins):
        iid = ins.id
        if iid in (XC.X86_INS_RET, XC.X86_INS_JMP):
            return True
        if iid == XC.X86_INS_CALL and self.prog.is_noreturn_import(ins):
            return True
        return False

    def translate(self, ins):
        iid = ins.id
        ops = ins.operands
        m = ins.mnemonic
        E = self.emit
        size = ops[0].size if ops else 4

        if iid == XC.X86_INS_MOV:
            E(self.write(ops[0], self.read(ops[1], ops[0].size)))
        elif iid == XC.X86_INS_MOVZX:
            E(self.write(ops[0], self.read(ops[1])))
        elif iid == XC.X86_INS_MOVSX:
            s = ops[1].size
            t = {1: "int8_t", 2: "int16_t"}[s]
            E(self.write(ops[0], "(uint32_t)(int32_t)(%s)(%s)" % (t, self.read(ops[1]))))
        elif iid == XC.X86_INS_LEA:
            E(self.write(ops[0], self.addr(ops[1])))
        elif iid == XC.X86_INS_PUSH:
            v = self.read(ops[0], 4) if ops[0].type != XC.X86_OP_IMM else \
                "0x%xu" % (ops[0].imm & 0xFFFFFFFF)
            E("{uint32_t t=%s; esp-=4; WR32(esp,t);}" % v)
        elif iid == XC.X86_INS_POP:
            if ops[0].type == XC.X86_OP_REG and ops[0].reg == XC.X86_REG_ESP:
                E("esp=RD32(esp);")
            else:
                E("{uint32_t t=RD32(esp); esp+=4; %s}" % self.write(ops[0], "t"))
        elif iid == XC.X86_INS_XCHG:
            a, b = self.read(ops[0]), self.read(ops[1])
            E("{uint32_t t=%s; %s %s}" % (a, self.write(ops[0], b), self.write(ops[1], "t")))
        elif iid in (XC.X86_INS_ADD, XC.X86_INS_SUB, XC.X86_INS_CMP):
            a = self.read(ops[0])
            b = self.read(ops[1], size)
            op = "+" if iid == XC.X86_INS_ADD else "-"
            fo = "FO_ADD" if iid == XC.X86_INS_ADD else "FO_SUB"
            E("{uint32_t a=%s, b=%s, r=(a%sb)&%s;" % (a, b, op, mask(size)))
            self.flags(fo, "a", "b", "r", size * 8)
            if iid != XC.X86_INS_CMP:
                E(self.write(ops[0], "r"))
            E("}")
        elif iid in (XC.X86_INS_AND, XC.X86_INS_OR, XC.X86_INS_XOR, XC.X86_INS_TEST):
            a = self.read(ops[0])
            b = self.read(ops[1], size)
            op = {XC.X86_INS_AND: "&", XC.X86_INS_TEST: "&", XC.X86_INS_OR: "|",
                  XC.X86_INS_XOR: "^"}[iid]
            if iid == XC.X86_INS_XOR and ops[0].type == XC.X86_OP_REG and \
                    ops[1].type == XC.X86_OP_REG and ops[0].reg == ops[1].reg:
                E("{uint32_t r=0;")
            else:
                E("{uint32_t r=(%s%s%s)&%s;" % (a, op, b, mask(size)))
            self.flags("FO_LOGIC", "0", "0", "r", size * 8)
            if iid != XC.X86_INS_TEST:
                E(self.write(ops[0], "r"))
            E("}")
        elif iid in (XC.X86_INS_INC, XC.X86_INS_DEC):
            a = self.read(ops[0])
            d = "+1" if iid == XC.X86_INS_INC else "-1"
            sign = sign_bit(size)
            ofc = ("r==0x%xu" % sign) if iid == XC.X86_INS_INC else ("r==0x%xu" % (sign - 1))
            E("{uint32_t cf=FL_CF; uint32_t r=(%s%s)&%s;" % (a, d, mask(size)))
            self.flags("FO_X", "0", "0", "r", size * 8, "cf", "(%s)" % ofc)
            E(self.write(ops[0], "r"))
            E("}")
        elif iid == XC.X86_INS_NEG:
            a = self.read(ops[0])
            E("{uint32_t a=%s, r=(0u-a)&%s;" % (a, mask(size)))
            self.flags("FO_SUB", "0", "a", "r", size * 8)
            E(self.write(ops[0], "r"))
            E("}")
        elif iid == XC.X86_INS_NOT:
            E(self.write(ops[0], "~%s" % self.read(ops[0])))
        elif iid in (XC.X86_INS_ADC, XC.X86_INS_SBB):
            a = self.read(ops[0])
            b = self.read(ops[1], size)
            mk = mask(size)
            sgn = sign_bit(size)
            if iid == XC.X86_INS_ADC:
                E("{uint32_t a=%s, b=%s, c=FL_CF; uint64_t w=(uint64_t)a+b+c; uint32_t r=(uint32_t)w&%s;"
                  % (a, b, mk))
                cf = "(w>%s)" % mk
                of = "((((a^r)&(b^r))&0x%xu)!=0)" % sgn
            else:
                E("{uint32_t a=%s, b=%s, c=FL_CF; uint64_t w=(uint64_t)a-b-c; uint32_t r=(uint32_t)w&%s;"
                  % (a, b, mk))
                cf = "((uint64_t)a<(uint64_t)b+c)"
                of = "((((a^b)&(a^r))&0x%xu)!=0)" % sgn
            self.flags("FO_X", "0", "0", "r", size * 8, cf, of)
            E(self.write(ops[0], "r"))
            E("}")
        elif iid in (XC.X86_INS_SHL, XC.X86_INS_SHR, XC.X86_INS_SAR):
            a = self.read(ops[0])
            if len(ops) == 1:
                cnt = "1"
            else:
                cnt = self.read(ops[1], 1)
            bits = size * 8
            E("{uint32_t c=(%s)&31;" % cnt)
            E(" if(c){uint32_t a=%s, r, cf, of;" % a)
            if iid == XC.X86_INS_SHL:
                E("  r=(uint32_t)((uint64_t)a<<c)&%s; cf=(uint32_t)(((uint64_t)a>>(%d-c))&1); "
                  "of=((r>>%d)&1)^cf;" % (mask(size), bits, bits - 1))
            elif iid == XC.X86_INS_SHR:
                E("  r=(a>>c)&%s; cf=(a>>(c-1))&1; of=(a>>%d)&1;" % (mask(size), bits - 1))
            else:
                st = {1: "int8_t", 2: "int16_t", 4: "int32_t"}[size]
                E("  r=(uint32_t)((int32_t)(%s)a>>c)&%s; cf=(uint32_t)(((int32_t)(%s)a>>(c-1))&1); of=0;"
                  % (st, mask(size), st))
            self.flags("FO_X", "0", "0", "r", bits, "cf", "of")
            E("  " + self.write(ops[0], "r") + "}}")
        elif iid == XC.X86_INS_IMUL:
            if len(ops) == 1 and size == 1:
                E("{int32_t p=(int32_t)(int8_t)(eax&0xff)*(int8_t)%s; eax=(eax&0xffff0000u)|((uint32_t)p&0xffffu);"
                  % self.read(ops[0]))
                self.flags("FO_X", "0", "0", "(uint32_t)p&0xffu", 8, "(p!=(int8_t)p)", "(p!=(int8_t)p)")
                E("}")
            elif len(ops) == 1:
                if size != 4:
                    raise Unsupported("imul16")
                E("{int64_t p=(int64_t)(int32_t)eax*(int32_t)%s; eax=(uint32_t)p; "
                  "edx=(uint32_t)((uint64_t)p>>32);" % self.read(ops[0]))
                self.flags("FO_X", "0", "0", "eax", 32, "(p!=(int32_t)p)", "(p!=(int32_t)p)")
                E("}")
            else:
                if len(ops) == 2:
                    a, b = self.read(ops[0]), self.read(ops[1], size)
                else:
                    a, b = self.read(ops[1]), self.read(ops[2], size)
                if size != 4:
                    raise Unsupported("imul16")
                E("{int64_t p=(int64_t)(int32_t)%s*(int32_t)%s; uint32_t r=(uint32_t)p;" % (a, b))
                self.flags("FO_X", "0", "0", "r", 32, "(p!=(int32_t)p)", "(p!=(int32_t)p)")
                E(self.write(ops[0], "r"))
                E("}")
        elif iid == XC.X86_INS_MUL:
            if size != 4:
                raise Unsupported("mul8")
            E("{uint64_t p=(uint64_t)eax*%s; eax=(uint32_t)p; edx=(uint32_t)(p>>32);"
              % self.read(ops[0]))
            self.flags("FO_X", "0", "0", "eax", 32, "(edx!=0)", "(edx!=0)")
            E("}")
        elif iid == XC.X86_INS_IDIV:
            if size != 4:
                raise Unsupported("idiv8")
            E("{int64_t n=(int64_t)(((uint64_t)edx<<32)|eax); int32_t d=(int32_t)%s; "
              "if(!d) rc_trap(0x%x,\"divide by zero\"); eax=(uint32_t)(int32_t)(n/d); "
              "edx=(uint32_t)(int32_t)(n%%d);}" % (self.read(ops[0]), ins.address))
        elif iid == XC.X86_INS_DIV:
            if size != 4:
                raise Unsupported("div8")
            E("{uint64_t n=((uint64_t)edx<<32)|eax; uint32_t d=%s; "
              "if(!d) rc_trap(0x%x,\"divide by zero\"); eax=(uint32_t)(n/d); edx=(uint32_t)(n%%d);}"
              % (self.read(ops[0]), ins.address))
        elif iid == XC.X86_INS_CDQ:
            E("edx=(uint32_t)((int32_t)eax>>31);")
        elif iid in JCC or iid in BRANCH_COND:
            if iid not in JCC:
                raise Unsupported(m)
            t = ops[0].imm
            E("if(%s) %s" % (cond_expr(JCC[iid]), self.goto(t)))
        elif iid in SETCC:
            E(self.write(ops[0], "(%s)?1u:0u" % cond_expr(SETCC[iid])))
        elif iid == XC.X86_INS_JMP:
            a = ins.address
            if ops[0].type == XC.X86_OP_IMM:
                E(self.goto(ops[0].imm))
            elif a in self.fn.jumptables:
                jt = self.fn.jumptables[a]
                idx = self.reg32(ops[0].mem.index)
                E("switch(%s){" % idx)
                for i, t in enumerate(jt["targets"]):
                    self.used_labels.add(t)
                    E(" case %d: goto L_%08x;" % (i, t))
                E(" default: rc_trap(0x%x,\"jump table index\");}" % a)
            elif a in self.fn.tailjumps:
                kind, slot = self.fn.tailjumps[a]
                E("{%s rc_call(RD32(0x%xu)); return;}" % (SYNC_OUT, slot))
            else:
                raise Unsupported("indirect jmp at %x" % a)
        elif iid == XC.X86_INS_CALL:
            ret = ins.address + ins.size
            if ops[0].type == XC.X86_OP_IMM:
                t = ops[0].imm
                E("esp-=4; WR32(esp,0x%xu); %s %s(); %s" % (ret, SYNC_OUT, self.fname(t), SYNC_IN))
            else:
                tgt = self.read(ops[0], 4)
                E("{uint32_t t=%s; esp-=4; WR32(esp,0x%xu); %s rc_call(t); %s}"
                  % (tgt, ret, SYNC_OUT, SYNC_IN))
                if self.prog.is_noreturn_import(ins):
                    E("rc_trap(0x%x,\"noreturn import returned\");" % ins.address)
        elif iid == XC.X86_INS_RET:
            n = ops[0].imm if ops else 0
            E("esp+=%d; %s return;" % (4 + n, SYNC_OUT))
        elif iid == XC.X86_INS_LEAVE:
            E("esp=ebp; ebp=RD32(esp); esp+=4;")
        elif iid == XC.X86_INS_NOP:
            pass
        elif iid == XC.X86_INS_CPUID:
            E("rc_cpuid(&eax,&ebx,&ecx,&edx);")
        elif iid == XC.X86_INS_PUSHFD:
            E("{uint32_t f=rc_eflags(F_op,F_a,F_b,F_r,F_sz,F_cf,F_of); esp-=4; WR32(esp,f);}")
        elif iid == XC.X86_INS_POPFD:
            E("{uint32_t f=RD32(esp); esp+=4; R.eflags_extra=f; F_op=FO_X; F_sz=32; "
              "F_r=(f&0x40)?0:1; F_cf=f&1; F_of=(f>>11)&1;}")
        elif iid in (XC.X86_INS_MOVSD, XC.X86_INS_MOVSB, XC.X86_INS_MOVSW,
                     XC.X86_INS_STOSD, XC.X86_INS_STOSB, XC.X86_INS_STOSW,
                     XC.X86_INS_CMPSB, XC.X86_INS_CMPSD):
            self.string_op(ins)
        elif iid == XC.X86_INS_MOVAPD:
            pass    # SSE2 probe in the CRT: no XMM state is modelled
        elif m.startswith("f"):
            self.x87(ins)
        else:
            raise Unsupported(m + " " + ins.op_str)

    def string_op(self, ins):
        E = self.emit
        m = ins.mnemonic
        rep = m.startswith("rep ")
        repe = m.startswith("repe ") or m.startswith("repz ")
        base = m.split()[-1]
        sz = {"b": 1, "w": 2, "d": 4}[base[-1]]
        bits = sz * 8
        if base.startswith("movs"):
            body = "WR%d(edi,RD%d(esi)); esi+=%d; edi+=%d;" % (bits, bits, sz, sz)
            E(("while(ecx){%s ecx--;}" % body) if rep else body)
        elif base.startswith("stos"):
            v = {1: "eax&0xffu", 2: "eax&0xffffu", 4: "eax"}[sz]
            if rep and sz == 4:
                E("rc_stos32(edi,eax,ecx); edi+=ecx*4; ecx=0;")
            elif rep and sz == 1:
                E("memset(M+edi,(int)(eax&0xff),ecx); edi+=ecx; ecx=0;")
            else:
                body = "WR%d(edi,%s); edi+=%d;" % (bits, v, sz)
                E(("while(ecx){%s ecx--;}" % body) if rep else body)
        elif base.startswith("cmps"):
            if not repe:
                raise Unsupported("cmps without repe")
            E("while(ecx){uint32_t a=RD%d(esi), b=RD%d(edi); esi+=%d; edi+=%d; ecx--; "
              "F_op=FO_SUB; F_a=a; F_b=b; F_r=(a-b)&%s; F_sz=%d; if(a!=b) break;}"
              % (bits, bits, sz, sz, mask(sz), bits))
        else:
            raise Unsupported(m)

    # ── x87 ──────────────────────────────────────────────────────────────────
    def fsrc(self, op):
        if op.type == XC.X86_OP_REG:
            return "ST(%d)" % FPR[op.reg]
        if op.type == XC.X86_OP_MEM:
            if op.size == 4:
                return "(double)RDF32(%s)" % self.addr(op)
            if op.size == 8:
                return "RDF64(%s)" % self.addr(op)
        raise Unsupported("fpu operand")

    def x87(self, ins):
        E = self.emit
        m = ins.mnemonic
        ops = ins.operands
        if m == "fld":
            E("{double v=%s; FPUSH(v);}" % self.fsrc(ops[0]))
        elif m == "fild":
            o = ops[0]
            t = {2: "(int16_t)RD16", 4: "(int32_t)RD32", 8: "(int64_t)RD64"}[o.size]
            E("FPUSH((double)%s(%s));" % (t, self.addr(o)))
        elif m == "fld1":
            E("FPUSH(1.0);")
        elif m == "fldz":
            E("FPUSH(0.0);")
        elif m in ("fst", "fstp"):
            o = ops[0]
            if o.type == XC.X86_OP_REG:
                E("ST(%d)=ST(0);" % FPR[o.reg])
            elif o.size == 4:
                E("WRF32(%s,(float)ST(0));" % self.addr(o))
            elif o.size == 8:
                E("WRF64(%s,ST(0));" % self.addr(o))
            else:
                raise Unsupported("fst size")
            if m == "fstp":
                E("FPOP();")
        elif m == "fistp":
            o = ops[0]
            if o.size == 4:
                E("WR32(%s,(uint32_t)rc_fist32(ST(0))); FPOP();" % self.addr(o))
            elif o.size == 8:
                E("WR64(%s,(uint64_t)rc_fist64(ST(0))); FPOP();" % self.addr(o))
            else:
                raise Unsupported("fistp size")
        elif m == "fchs":
            E("ST(0)=-ST(0);")
        elif m == "fxch":
            # capstone lists fxch st(i) as [st(0), st(i)]: take the last one
            i = FPR[ops[-1].reg] if ops else 1
            if i:
                E("{double t=ST(0); ST(0)=ST(%d); ST(%d)=t;}" % (i, i))
        elif m in ("fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr",
                   "faddp", "fsubp", "fsubrp", "fmulp", "fdivp", "fdivrp"):
            pop = m.endswith("p")
            base = m[:-1] if pop else m
            opsym = {"fadd": "+", "fsub": "-", "fsubr": "-", "fmul": "*", "fdiv": "/",
                     "fdivr": "/"}[base]
            rev = base in ("fsubr", "fdivr")
            if len(ops) == 0:
                dst, src = "ST(1)", "ST(0)"
            elif len(ops) == 1:
                if ops[0].type == XC.X86_OP_MEM:
                    dst, src = "ST(0)", self.fsrc(ops[0])
                elif pop:
                    dst, src = "ST(%d)" % FPR[ops[0].reg], "ST(0)"
                else:
                    dst, src = "ST(0)", "ST(%d)" % FPR[ops[0].reg]
            else:
                dst, src = self.fsrc(ops[0]), self.fsrc(ops[1])
            if rev:
                E("%s=(%s)%s(%s);" % (dst, src, opsym, dst))
            else:
                E("%s=(%s)%s(%s);" % (dst, dst, opsym, src))
            if pop:
                E("FPOP();")
        elif m in ("fcom", "fcomp", "fcompp"):
            src = "ST(1)" if not ops else self.fsrc(ops[0])
            E("rc_fcom(ST(0),%s);" % src)
            if m == "fcomp":
                E("FPOP();")
            elif m == "fcompp":
                E("FPOP(); FPOP();")
        elif m == "fnstsw":
            o = ops[0]
            if o.type == XC.X86_OP_REG:
                E(self.write(o, "rc_fsw()"))
            else:
                E("WR16(%s,rc_fsw());" % self.addr(o))
        elif m == "fnstcw":
            E("WR16(%s,R.fcw);" % self.addr(ops[0]))
        else:
            raise Unsupported(m)


def mask(size):
    return {1: "0xffu", 2: "0xffffu", 4: "0xffffffffu"}[size]


def sign_bit(size):
    return {1: 0x80, 2: 0x8000, 4: 0x80000000}[size]


HEADER = '#include "recomp_rt.h"\n'


def emit_all(outdir: pathlib.Path, per_file=40):
    img = Image()
    prog = Program(img).build(load_seeds(img))
    outdir.mkdir(parents=True, exist_ok=True)
    funcs = sorted(prog.funcs.values(), key=lambda f: f.entry)
    decls = []
    failures = []
    chunks = []
    cur = []
    for idx, fn in enumerate(funcs):
        name = "f_%08x" % fn.entry
        decls.append("void %s(void);" % name)
        if fn.entry in NATIVE:
            continue
        em = FnEmitter(prog, fn, prog.entries)
        try:
            lines = em.body()
        except Unsupported as e:
            failures.append((fn.entry, str(e)))
            lines = ['  rc_trap(0x%x,"untranslated: %s");' % (fn.entry, str(e).replace('"', "'"))]
        body = ["void %s(void) {" % name]
        if HOOKS:
            body.append("  rc_calls[%d]++;" % idx)
            body.append("  if (__builtin_expect(rc_hook[%d] != 0, 0)) { rc_hook[%d](); return; }" % (idx, idx))
        body += ["  uint32_t " + ", ".join("%s=R.%s" % (r, r) for r in REGS) + ";",
                "  uint32_t F_a=0,F_b=0,F_r=0,F_cf=0,F_of=0; int F_op=FO_LOGIC,F_sz=32;",
                "  (void)F_a;(void)F_b;(void)F_r;(void)F_cf;(void)F_of;(void)F_op;(void)F_sz;"]
        body += lines
        body += ["  rc_trap(0x%x,\"fell off function end\");" % fn.entry, "}", ""]
        cur.append("\n".join(body))
        if len(cur) >= per_file:
            chunks.append(cur)
            cur = []
    if cur:
        chunks.append(cur)
    (outdir / "lf2_funcs.h").write_text(
        "#pragma once\n" + ("extern rc_fn rc_hook[];\nextern unsigned long long rc_calls[];\n" if HOOKS else "") + "\n".join(decls) + "\n"
        + "\n".join("void %s(void);" % v for v in sorted(set(NATIVE.values()) | set(WRAP.values())))
        + "\n")
    for i, ch in enumerate(chunks):
        (outdir / ("lf2_gen_%02d.c" % i)).write_text(
            HEADER + '#include "lf2_funcs.h"\n\n' + "\n".join(ch))
    # dispatch table for indirect calls
    tab = ["#include \"recomp_rt.h\"", "#include \"lf2_funcs.h\"",
           "const rc_fn_entry rc_fn_table[] = {"]
    for fn in funcs:
        tab.append("  {0x%08xu, %s}," % (fn.entry, NATIVE.get(fn.entry, WRAP.get(fn.entry, "f_%08x" % fn.entry))))
    tab.append("};")
    tab.append("const unsigned rc_fn_count = %d;" % len(funcs))
    if HOOKS:
        tab.append("rc_fn rc_hook[%d];" % len(funcs))
        tab.append("unsigned long long rc_calls[%d];" % len(funcs))
    (outdir / "lf2_dispatch.c").write_text("\n".join(tab) + "\n")
    # the executable itself, embedded in the program (host_sdl.cpp): the game's
    # data/rdata/resources then come from the eboot and lf2.exe is not needed
    # on the device. The sha in the comment makes the .S change with the exe.
    import hashlib
    exe_bytes = pathlib.Path(EXE).read_bytes()
    bin_path = (outdir / "lf2_exe.bin").resolve()
    bin_path.write_bytes(exe_bytes)
    # In .data (RW segment): with the 3.4 MB blob in .rodata the text segment
    # leaves no room for the SCE tables and vita-elf-create fails.
    (outdir / "lf2_image.S").write_text(
        "/* generated by tools/recomp/emit.py - lf2.exe sha256 %s */\n"
        "    .data\n"
        "    .balign 16\n"
        "    .global lf2_exe_image\n"
        "    .type lf2_exe_image, %%object\n"
        "lf2_exe_image:\n"
        "    .incbin \"%s\"\n"
        "    .size lf2_exe_image, . - lf2_exe_image\n"
        "    .global lf2_exe_image_end\n"
        "    .type lf2_exe_image_end, %%object\n"
        "lf2_exe_image_end:\n"
        "    .byte 0\n"
        "    .size lf2_exe_image_end, 1\n"
        "#ifdef __linux__\n"
        "    .section .note.GNU-stack,\"\",%%progbits\n"
        "#endif\n" % (hashlib.sha256(exe_bytes).hexdigest(), str(bin_path).replace("\\", "/")))
    return prog, failures


if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/recomp")
    prog, failures = emit_all(out)
    print("functions:", len(prog.funcs), "untranslated:", len(failures))
    for f in failures:
        print("  %08x %s" % f)
