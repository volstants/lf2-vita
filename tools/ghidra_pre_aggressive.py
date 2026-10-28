# -*- coding: utf-8 -*-
# Ghidra headless PRE-script: turn on the aggressive analysis options before the
# auto-analysis runs. Candidate discovery is not verified function recovery.
# Use ghidra_audit.py to measure coverage and retain evidence for boundaries.
# Keep pure ASCII. PYTHON 3: o Ghidra 12 roda scripts via PyGhidra, nao mais
# Jython. `except Exception, e:` (Py2) era SyntaxError e o script morria em
# silencio — o headless seguia com a analise PADRAO e ninguem percebia.
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
    # broad discovery for subsequent validation. Spurious code is NOT harmless:
    # padding and embedded data must not become function entries merely because
    # they decode as instructions. "Known" (curated list) stays on.
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
        except Exception as e:
            print("[lf2-pre] skip %s (%s)" % (k, e))
    else:
        print("[lf2-pre] option not present: %s" % k)

print("[lf2-pre] aggressive analysis enabled")
