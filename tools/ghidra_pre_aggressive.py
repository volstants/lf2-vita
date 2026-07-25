# -*- coding: utf-8 -*-
# Ghidra headless PRE-script: turn on the aggressive analysis options before the
# auto-analysis runs. Raises function coverage a lot on stripped MSVC binaries
# like lf2.exe (default run found only 351 functions).
# Keep pure ASCII (Jython).
#
#   @category LF2
from ghidra.program.model.listing import Program

opts = currentProgram.getOptions(Program.ANALYSIS_PROPERTIES)

wanted = {
    # finds code the reference-following pass misses (the big one)
    "Aggressive Instruction Finder": True,
    "Aggressive Instruction Finder.Create Analysis Bookmarks": False,
    # better signatures/params on what is found
    "Decompiler Parameter ID": True,
    "Decompiler Switch Analysis": True,
    # MSVC specifics
    "Windows x86 PE RTTI Analyzer": True,
    "Demangler Microsoft": True,
    "Function ID": True,
    "Shared Return Calls": True,
    # OFF on purpose: this one *guesses* which functions never return and then
    # stops following flow after every call to them. lf2.exe is MSVC 2005 built
    # with FPO (only 15 'push ebp; mov ebp,esp' in the whole .text), which makes
    # that heuristic shakier — a false positive silently drops real code. We want
    # maximum coverage for grep-based lookups, and spurious code is harmless
    # while missing code is not. "Known" (curated list) stays on.
    "Non-Returning Functions - Discovered": False,
    "Non-Returning Functions - Known": True,
    "Create Address Tables": True,
    "Scalar Operand References": True,
}

names = list(opts.getOptionNames())
for k, v in wanted.items():
    if k in names:
        try:
            opts.setBoolean(k, v)
            print("[lf2-pre] %s = %s" % (k, v))
        except Exception, e:
            print("[lf2-pre] skip %s (%s)" % (k, e))
    else:
        print("[lf2-pre] option not present: %s" % k)

print("[lf2-pre] aggressive analysis enabled")
