# -*- coding: utf-8 -*-
# Ghidra headless POST-script.
#   1. Recovers extra functions the analyzer missed (disassembles orphan code in
#      executable blocks and promotes call targets / orphan instructions).
#   2. Decompiles EVERY function into one .c file.
# Run via analyzeHeadless (see tools/DECOMPILE.md). Jython 2.7 - keep pure ASCII.
#
#   @category LF2
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.program.model.symbol import RefType

out_path = os.environ.get("LF2_DECOMP_OUT", "lf2_decomp.c")
monitor = ConsoleTaskMonitor()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

before = fm.getFunctionCount()

# ---- 1a. disassemble undefined bytes inside executable blocks ----------------
for blk in mem.getBlocks():
    if not blk.isExecute():
        continue
    print("[lf2] scanning block %s (%s - %s)" % (blk.getName(), blk.getStart(), blk.getEnd()))
    addr = blk.getStart()
    end = blk.getEnd()
    n = 0
    while addr is not None and addr.compareTo(end) < 0:
        d = listing.getUndefinedDataAt(addr)
        if d is not None:
            try:
                DisassembleCommand(addr, None, True).applyTo(currentProgram, monitor)
                n += 1
            except Exception:
                pass
        addr = addr.next()
        if n and n % 2000 == 0:
            print("[lf2]   disassembled %d spots" % n)
    print("[lf2]   block done (%d disassembly attempts)" % n)

# ---- 1b. promote CALL targets that have no function -------------------------
made = 0
for fn in list(fm.getFunctions(True)):
    pass  # (kept for API warm-up; the real pass is below)

refmgr = currentProgram.getReferenceManager()
it = refmgr.getReferenceIterator(currentProgram.getMinAddress())
seen = set()
while it.hasNext():
    ref = it.next()
    if not ref.getReferenceType().isCall():
        continue
    tgt = ref.getToAddress()
    if tgt is None or tgt in seen:
        continue
    seen.add(tgt)
    if listing.getFunctionContaining(tgt) is None and listing.getInstructionAt(tgt) is not None:
        if CreateFunctionCmd(tgt).applyTo(currentProgram, monitor):
            made += 1
print("[lf2] created %d functions from call targets" % made)

# ---- 1c. promote remaining orphan instruction runs ---------------------------
# IMPORTANT: skip INT3 (0xCC) padding. MSVC pads between functions with runs of
# int3; creating a "function" on each padding byte inflated a run from 351 to
# 2180 entries, 1573 of which were 1-byte swi(3) stubs. Only real code counts.
orph = 0
skipped_pad = 0
ins = listing.getInstructions(True)
while ins.hasNext():
    i = ins.next()
    a = i.getAddress()
    if listing.getFunctionContaining(a) is not None:
        continue
    if i.getMnemonicString().upper() in ("INT3", "INT 3"):
        skipped_pad += 1
        continue
    # only start a function where nothing flows into this address linearly
    prev = listing.getInstructionBefore(a)
    if prev is not None and prev.getMaxAddress().next() == a and prev.getFlowType().isFallthrough():
        continue
    if CreateFunctionCmd(a).applyTo(currentProgram, monitor):
        orph += 1
print("[lf2] created %d functions from orphan code (%d int3 padding bytes skipped)"
      % (orph, skipped_pad))

after = fm.getFunctionCount()
print("[lf2] functions: %d -> %d" % (before, after))

# ---- 2. decompile everything ------------------------------------------------
decomp = DecompInterface()
decomp.openProgram(currentProgram)

funcs = list(fm.getFunctions(True))
print("[lf2] decompiling %d functions -> %s" % (len(funcs), out_path))

f = open(out_path, "w")
try:
    f.write("// LF2 decompiled by Ghidra - reference only.\n")
    f.write("// %d functions from %s\n\n" % (len(funcs), currentProgram.getName()))
    done = 0
    for fn in funcs:
        try:
            res = decomp.decompileFunction(fn, 60, monitor)
            if res and res.decompileCompleted():
                f.write("\n/* ---- %s @ %s ---- */\n" % (fn.getName(), fn.getEntryPoint()))
                f.write(res.getDecompiledFunction().getC())
                done += 1
                if done % 500 == 0:
                    print("[lf2] %d/%d" % (done, len(funcs)))
        except Exception, e:
            print("[lf2] skip %s: %s" % (fn.getName(), e))
finally:
    f.close()
print("[lf2] done: %d functions written" % done)
