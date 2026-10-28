# -*- coding: utf-8 -*-
# Ghidra headless POST-script for Python 3 via PyGhidra.
# Export recognized functions only. Do not promote arbitrary orphan runs or
# disassemble padding as code. For evidence-based recovery, use ghidra_audit.py
# on an isolated project copy. See DECOMPILE.md and AUDITORIA_2026-09-20_GHIDRA.md.
# @category LF2
import io
import json
import os
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

out_path = os.environ.get("LF2_DECOMP_OUT", "lf2_decomp.c")
timeout = int(os.environ.get("LF2_DECOMP_TIMEOUT", "180"))
payload = int(os.environ.get("LF2_DECOMP_PAYLOAD_MB", "128"))
monitor = ConsoleTaskMonitor()
decomp = DecompInterface()
options = DecompileOptions()
options.setMaxPayloadMBytes(payload)
decomp.setOptions(options)
decomp.openProgram(currentProgram)
functions = list(currentProgram.getFunctionManager().getFunctions(True))
selected = set(a.strip().lower().replace('0x', '') for a in
               os.environ.get('LF2_DECOMP_ADDRESSES', '').split(',') if a.strip())
if selected:
    functions = [fn for fn in functions if str(fn.getEntryPoint()).lower() in selected]
    found = set(str(fn.getEntryPoint()).lower() for fn in functions)
    if found != selected:
        decomp.dispose()
        raise ValueError('Requested function addresses not found: %s' % sorted(selected - found))
statuses = []
print("[lf2] exporting %d recognized functions -> %s" % (len(functions), out_path))
try:
    with io.open(out_path, "w", encoding="utf-8") as output:
        output.write(u"// Ghidra pseudocode - reference only, not original source.\n")
        for number, fn in enumerate(functions, 1):
            status = {"address": str(fn.getEntryPoint()), "name": str(fn.getName()),
                      "completed": False, "error": ""}
            output.write(u"\n/* ---- %s @ %s ---- */\n" % (fn.getName(), fn.getEntryPoint()))
            try:
                result = decomp.decompileFunction(fn, timeout, monitor)
                status["completed"] = bool(result.decompileCompleted())
                status["error"] = str(result.getErrorMessage())
                if status["completed"]:
                    output.write(result.getDecompiledFunction().getC())
                else:
                    output.write(u"// Decompilation failed; see .status.json\n")
            except Exception as error:
                status["error"] = str(error)
                output.write(u"// Decompilation raised an exception; see .status.json\n")
            statuses.append(status)
            if not status["completed"] or number % 25 == 0:
                print("[lf2] %d/%d %s completed=%s %s" %
                      (number, len(functions), status["address"], status["completed"], status["error"]))
finally:
    decomp.dispose()
    with io.open(out_path + ".status.json", "w", encoding="utf-8") as output:
        output.write(json.dumps({"timeout_seconds": timeout, "max_payload_mb": payload,
                                 "functions": statuses}, indent=2))
print("[lf2] completed=%d failed=%d" %
      (sum(s["completed"] for s in statuses), sum(not s["completed"] for s in statuses)))
