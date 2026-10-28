"""Reproducible LF2 coverage audit. Operates only on an isolated project copy.

Requires the workspace venv with pyghidra, pefile and capstone. No game is run.
Run with --help. Percentages describe bytes, never semantic correctness.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding="utf-8")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ranges(values):
    result = []
    for value in sorted(values):
        if result and value == result[-1][1] + 1:
            result[-1][1] = value
        else:
            result.append([value, value])
    return result


def audit(program, root, output):
    import capstone
    import pefile
    from ghidra.program.model.address import AddressSet

    exe = root / "reference/decomp/lf2.exe"
    raw = exe.read_bytes()
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    listing = program.getListing()
    fm = program.getFunctionManager()
    refs = program.getReferenceManager()
    text_block = program.getMemory().getBlock(".text")
    lo, hi = int(text_block.getStart().getOffset()), int(text_block.getEnd().getOffset())
    scope = AddressSet(text_block.getStart(), text_block.getEnd())
    original_text = pe.get_data(lo - base, hi - lo + 1)
    import_map = {}
    imports = []
    for lib in pe.DIRECTORY_ENTRY_IMPORT:
        name = lib.dll.decode()
        entries = []
        for item in lib.imports:
            entry = {"name": item.name.decode() if item.name else None,
                     "ordinal": item.ordinal, "iat": f"{item.address:08x}"}
            entries.append(entry)
            import_map[item.address] = dict(entry, dll=name)
        imports.append({"dll": name, "entries": entries})
    write_json(output / "imports.json", imports)
    instructions, function_bytes, data_bytes = set(), set(), set()
    asm = []
    mismatches, decode_disagreements, orphan_instructions = [], [], []
    direct_call_targets = collections.defaultdict(list)
    indirect_branches, float_refs = [], collections.defaultdict(list)
    for instruction in listing.getInstructions(scope, True):
        address = instruction.getAddress()
        va, size = int(address.getOffset()), int(instruction.getLength())
        data = bytes(int(v) & 255 for v in instruction.getBytes())
        disk_data = pe.get_data(va - base, size)
        if data != disk_data:
            mismatches.append(f"{va:08x}")
        decoded = list(cs.disasm(disk_data, va, count=1))
        if not decoded or decoded[0].size != size:
            decode_disagreements.append({"address": f"{va:08x}", "ghidra": str(instruction),
                "bytes": data.hex(), "capstone": str(decoded[0]) if decoded else None})
        instructions.update(range(va, va + size))
        owner = fm.getFunctionContaining(address)
        owner_va = str(owner.getEntryPoint()) if owner else None
        asm.append(f"{va:08x}  {data.hex():30s} {str(instruction):65s} ; {owner_va or 'ORPHAN'}")
        if owner is None:
            orphan_instructions.append({"address": f"{va:08x}", "instruction": str(instruction), "bytes": data.hex()})
        flow = instruction.getFlowType()
        if flow.isComputed():
            indirect_branches.append({"address": f"{va:08x}", "function": owner_va,
                "instruction": str(instruction), "flows": [str(a) for a in instruction.getFlows()],
                "kind": str(flow)})
        for ref in refs.getReferencesFrom(address):
            target = ref.getToAddress()
            if not target.isMemoryAddress():
                continue
            target_va = int(target.getOffset())
            if ref.getReferenceType().isCall() and not flow.isComputed():
                direct_call_targets[target_va].append(f"{va:08x}")
            mnemonic = str(instruction.getMnemonicString())
            if ref.getReferenceType().isData() and mnemonic.startswith("F"):
                display = str(instruction).lower()
                size_hint = 8 if ("qword" in display or "double ptr" in display) else 4 if ("dword" in display or "float ptr" in display) else None
                float_refs[target_va].append({"from": f"{va:08x}", "function": owner_va,
                    "instruction": str(instruction), "size_hint": size_hint,
                    "integer_operand":mnemonic.startswith(('FI','FN'))})
    (output / "text.asm").write_text("\n".join(asm) + "\n", encoding="utf-8")
    for data in listing.getDefinedData(scope, True):
        start = int(data.getAddress().getOffset())
        data_bytes.update(range(start, start + int(data.getLength())))
    functions = []
    for fn in fm.getFunctions(True):
        entry = fn.getEntryPoint()
        va = int(entry.getOffset())
        body = [[int(r.getMinAddress().getOffset()), int(r.getMaxAddress().getOffset())]
                for r in fn.getBody().getAddressRanges()]
        for a, b in body:
            function_bytes.update(range(max(a, lo), min(b, hi) + 1))
        first = listing.getInstructionAt(entry)
        raw_first = bytes(int(v) & 255 for v in first.getBytes()) if first else b""
        imported = None
        if raw_first[:2] == b"\xff\x25" and len(raw_first) == 6:
            imported = import_map.get(struct.unpack_from("<I", raw_first, 2)[0])
        functions.append({"address": str(entry), "name": str(fn.getName()),
            "signature": str(fn.getSignature()), "signature_source": str(fn.getSignatureSource()),
            "is_thunk": bool(fn.isThunk()), "is_external": bool(fn.isExternal()),
            "no_return": bool(fn.hasNoReturn()), "body": [[f"{a:08x}",f"{b:08x}"] for a,b in body],
            "body_bytes": int(fn.getBody().getNumAddresses()),
            "first_instruction": str(first) if first else None, "import_jump": imported,
            "entry_refs": [{"from":str(r.getFromAddress()),"type":str(r.getReferenceType())}
                           for r in refs.getReferencesTo(entry)]})
    all_bytes = set(range(lo, hi + 1))
    undefined = all_bytes - instructions - data_bytes
    undefined_ranges = []
    for a, b in ranges(undefined):
        content = pe.get_data(a-base, b-a+1)
        uniform_padding = bool(content) and set(content) <= {0xcc, 0x90}
        undefined_ranges.append({"start":f"{a:08x}","end":f"{b:08x}","length":b-a+1,
            "padding_only_cc_90":uniform_padding,"bytes_preview":content[:32].hex()})
    float_constants = []
    for va, uses in sorted(float_refs.items()):
        data = pe.get_data(va-base, 8)
        if len(data) < 8:
            continue
        float_constants.append({"address":f"{va:08x}","file_offset":hex(pe.get_offset_from_rva(va-base)),
            "bytes": data.hex(), "float32_candidate":repr(struct.unpack('<f',data[:4])[0]),
            "float64_candidate":repr(struct.unpack('<d',data)[0]),"uses":uses})
    missing_targets = []
    for va, callers in sorted(direct_call_targets.items()):
        if not lo <= va <= hi:
            continue
        address = program.getAddressFactory().getDefaultAddressSpace().getAddress(va)
        if fm.getFunctionAt(address) is None:
            owner = fm.getFunctionContaining(address)
            missing_targets.append({"address":f"{va:08x}","callers":callers,
                "containing_function":str(owner.getEntryPoint()) if owner else None})
    bookmarks = [{"address":str(b.getAddress()),"type":str(b.getTypeString()),
        "category":str(b.getCategory()),"comment":str(b.getComment())}
        for b in program.getBookmarkManager().getBookmarksIterator()]
    exports = {f.stem: json.loads(f.read_text(encoding="utf-8"))
               for f in (root / '.ghidra-exports').glob('[0-9]*.json')}
    current = {f['address'] for f in functions}
    old_c = (root / 'reference/decomp/lf2_decomp.c').read_text(errors='replace')
    old_entries = set(re.findall(r'^/\* ---- .*? @ ([0-9a-fA-F]+) ---- \*/', old_c, re.M))
    opts = program.getOptions("Analyzers")
    analysis_options = {str(k):str(opts.getObject(k, None)) for k in opts.getOptionNames()}
    summary = {"executable_sha256":digest(exe),"program_executable_sha256":str(program.getExecutableSHA256()),
        "language":str(program.getLanguageID()),"compiler":str(program.getCompilerSpec().getCompilerSpecID()),
        "text_start":hex(lo),"text_end":hex(hi),"text_mapped_bytes":len(all_bytes),
        "text_pe_virtual_bytes":next(s.Misc_VirtualSize for s in pe.sections if s.Name.rstrip(b'\0')==b'.text'),
        "instruction_bytes":len(instructions),"defined_data_bytes":len(data_bytes),
        "function_body_bytes":len(function_bytes),"instruction_bytes_outside_functions":len(instructions-function_bytes),
        "undefined_bytes":len(undefined),"undefined_padding_bytes":sum(r['length'] for r in undefined_ranges if r['padding_only_cc_90']),
        "instruction_count":len(asm),"function_count":len(functions),
        "import_jump_functions":sum(bool(f['import_jump']) for f in functions),
        "ghidra_disk_instruction_byte_mismatches":mismatches,"capstone_instruction_length_disagreements":decode_disagreements,
        "missing_direct_call_targets":missing_targets,"export_count":len(exports),
        "current_missing_in_json":sorted(current-exports.keys()),"json_missing_in_current":sorted(exports.keys()-current),
        "old_c_count":len(old_entries),"old_c_only":sorted(old_entries-current),"current_only_vs_old_c":sorted(current-old_entries),
        "bookmarks_by_type":dict(collections.Counter(b['type'] for b in bookmarks)),
        "analysis_options":analysis_options}
    for name,obj in [('summary',summary),('functions',functions),('undefined_ranges',undefined_ranges),
                     ('orphan_instructions',orphan_instructions),('indirect_branches',indirect_branches),
                     ('float_constants',float_constants),('bookmarks',bookmarks)]:
        write_json(output / (name+'.json'),obj)
    print(json.dumps({k:v for k,v in summary.items() if not isinstance(v,(list,dict))},indent=2),flush=True)


def probe(program, output):
    import time
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.util.task import TaskMonitor
    fm, listing = program.getFunctionManager(), program.getListing()
    rm = program.getReferenceManager()
    candidates = []
    for ins in listing.getInstructions(True):
        a = ins.getAddress()
        if fm.getFunctionContaining(a) is not None:
            continue
        rs = list(rm.getReferencesTo(a))
        prev = listing.getInstructionBefore(a)
        linear = prev is not None and prev.getFallThrough() == a
        data_ref = any(r.getReferenceType().isData() for r in rs)
        if linear or (rs and not data_ref):
            continue
        body = CreateFunctionCmd.getFunctionBody(program, a)
        candidates.append({'address':str(a),'first':str(ins),'body_bytes':int(body.getNumAddresses()),
            'refs':[{'from':str(r.getFromAddress()),'type':str(r.getReferenceType())} for r in rs],
            'body':[[str(r.getMinAddress()),str(r.getMaxAddress())] for r in body.getAddressRanges()]})
    write_json(output/'orphan_candidates.json',candidates)
    print('Orphan roots:',len(candidates),flush=True)
    target = fm.getFunctionAt(program.getAddressFactory().getDefaultAddressSpace().getAddress(0x41bc90))
    attempts = []
    for enlarged in [False,True]:
        dec = DecompInterface()
        options = DecompileOptions()
        if enlarged:
            options.setMaxPayloadMBytes(128)
            options.setMaxInstructions(1000000)
        dec.setOptions(options)
        dec.openProgram(program)
        started = time.monotonic()
        print('Decompiling 0041bc90, enlarged:',enlarged,flush=True)
        try:
            result = dec.decompileFunction(target,180,TaskMonitor.DUMMY)
            record = {'enlarged':enlarged,'completed':bool(result.decompileCompleted()),
                'error':str(result.getErrorMessage()),'seconds':time.monotonic()-started,
                'max_payload_mb':options.getMaxPayloadMBytes(),'max_instructions':options.getMaxInstructions()}
            if result.decompileCompleted():
                code=str(result.getDecompiledFunction().getC())
                (output/('0041bc90_'+('enlarged' if enlarged else 'default')+'.c')).write_text(code,encoding='utf-8')
                record['c_characters']=len(code)
            attempts.append(record)
            write_json(output/'decompile_attempts.json',attempts)
            print(json.dumps(record),flush=True)
        finally:
            dec.dispose()


def enhance(program, root, output):
    """Recover referenced code roots, never arbitrary padding or switch cases."""
    import pefile
    import pyghidra
    from ghidra.app.cmd.function import CreateFunctionCmd, ApplyFunctionSignatureCmd
    from ghidra.program.model.symbol import SourceType
    from ghidra.program.model.data import FunctionDefinition, Pointer, TypeDef
    from ghidra.util.task import TaskMonitor
    fm, listing, rm = program.getFunctionManager(), program.getListing(), program.getReferenceManager()
    space = program.getAddressFactory().getDefaultAddressSpace()
    pe = pefile.PE(str(root/'reference/decomp/lf2.exe'))
    base = pe.OPTIONAL_HEADER.ImageBase
    candidates = collections.defaultdict(list)
    for ins in listing.getInstructions(True):
        a = ins.getAddress()
        if fm.getFunctionContaining(a) is not None:
            continue
        for ref in rm.getReferencesTo(a):
            if ref.getReferenceType().isData():
                candidates[int(a.getOffset())].append({'from':str(ref.getFromAddress()),'kind':'ghidra_data_reference'})
    # Independently find address-taken callbacks, including the dispatch table
    # whose scalar operands had not been turned into Ghidra references.
    pointer_inventory = []
    for section in pe.sections:
        if section.Name.rstrip(b'\0') not in (b'.rdata',b'.data') or not section.SizeOfRawData:
            continue
        data = section.get_data()
        for offset in range(0, len(data)-3, 4):
            target = struct.unpack_from('<I', data, offset)[0]
            if not 0x401000 <= target < 0x44630a:
                continue
            a = space.getAddress(target)
            if fm.getFunctionContaining(a) is not None:
                continue
            source = base + section.VirtualAddress + offset
            row = {'from':f'{source:08x}','to':f'{target:08x}',
                   'section':section.Name.rstrip(b'\0').decode(),'instruction':str(listing.getInstructionAt(a))}
            pointer_inventory.append(row)
            if listing.getInstructionAt(a) is not None:
                candidates[target].append({'from':f'{source:08x}','kind':'raw_aligned_pointer'})
    write_json(output/'raw_code_pointers.json', pointer_inventory)
    decisions = []
    with pyghidra.transaction(program,'Recover referenced orphan entry points'):
        for va,evidence in sorted(candidates.items()):
            a = space.getAddress(va)
            row = {'address':str(a),'evidence':evidence}
            owner = fm.getFunctionContaining(a)
            ins = listing.getInstructionAt(a)
            incoming = list(rm.getReferencesTo(a))
            prev = listing.getInstructionBefore(a)
            if owner is not None:
                row.update(action='skip',reason='already belongs to function',owner=str(owner.getEntryPoint()))
            elif ins is None:
                row.update(action='skip',reason='not an instruction start')
            elif any(r.getReferenceType().isJump() for r in incoming):
                row.update(action='skip',reason='incoming jump: block or switch case, not proven entry')
            elif prev is not None and prev.getFallThrough() == a:
                row.update(action='skip',reason='incoming linear flow')
            else:
                cmd = CreateFunctionCmd(a)
                ok = bool(cmd.applyTo(program,TaskMonitor.DUMMY))
                row.update(action='created' if ok else 'failed',status=str(cmd.getStatusMsg()))
            decisions.append(row)
        # These API callback types already exist in the imported Windows types.
        # Their use is confirmed by the existing assembly at 43bdde/43bdfc and
        # 43c47b; no inferred game-object layouts are introduced here.
        for va,type_name,new_name in [(0x43b3d0,'WNDPROC','LF2_WndProc'),
                                       (0x43c240,'LPTHREAD_START_ROUTINE','LF2_ThreadProc_0043c240')]:
            fn = fm.getFunctionAt(space.getAddress(va))
            if fn is None:
                continue
            for dt in program.getDataTypeManager().getAllDataTypes():
                if str(dt.getName()) != type_name:
                    continue
                while isinstance(dt,(TypeDef,Pointer)):
                    dt = dt.getBaseDataType() if isinstance(dt,TypeDef) else dt.getDataType()
                if isinstance(dt,FunctionDefinition):
                    cmd = ApplyFunctionSignatureCmd(fn.getEntryPoint(),dt,SourceType.ANALYSIS)
                    ok = bool(cmd.applyTo(program,TaskMonitor.DUMMY))
                    decisions.append({'address':str(fn.getEntryPoint()),'action':'apply_callback_signature',
                                      'type':type_name,'success':ok,'status':str(cmd.getStatusMsg())})
                    fn.setName(new_name,SourceType.ANALYSIS)
                break
    write_json(output/'recovery_decisions.json',decisions)
    program.save('Isolated LF2 coverage audit',TaskMonitor.DUMMY)
    print('Recovered entry points:',sum(r['action']=='created' for r in decisions),flush=True)


def export_rich(program, output, only=None):
    """Export all functions, with explicit failures and real data references."""
    import time
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.util.task import TaskMonitor
    from ghidra_ai_bridge.exporters.runner import _extract_high_ir, export_structs, export_strings, export_globals, export_vtables
    folder = output/'exports'
    folder.mkdir(exist_ok=True)
    fm, listing, rm = program.getFunctionManager(),program.getListing(),program.getReferenceManager()
    mem, sm = program.getMemory(),program.getSymbolTable()
    dec = DecompInterface()
    opts = DecompileOptions()
    opts.setMaxPayloadMBytes(128)
    opts.setMaxInstructions(1000000)
    dec.setOptions(opts)
    dec.openProgram(program)
    funcs = [f for f in fm.getFunctions(True) if not only or str(f.getEntryPoint()) in only]
    index_path = folder/'_index.json'
    index = json.loads(index_path.read_text(encoding='utf-8')) if only and index_path.exists() else {}
    try:
        for n,fn in enumerate(funcs,1):
            va = str(fn.getEntryPoint())
            started = time.monotonic()
            result = dec.decompileFunction(fn,180,TaskMonitor.DUMMY)
            success = bool(result.decompileCompleted())
            code = str(result.getDecompiledFunction().getC()) if success else '// Decompilation failed: '+str(result.getErrorMessage())
            pcode,cfg,pe,ce = _extract_high_ir(result) if success else ([],[],[str(result.getErrorMessage())],[str(result.getErrorMessage())])
            instructions,asm,data_refs,callees = [],[],[],[]
            for ins in listing.getInstructions(fn.getBody(),True):
                a = ins.getAddress()
                b = bytes(int(v)&255 for v in ins.getBytes()).hex()
                asm.append(f'{a}  {ins}')
                instructions.append({'address':str(a),'bytes':b,'instruction':str(ins)})
                for ref in rm.getReferencesFrom(a):
                    target = ref.getToAddress()
                    if ref.getReferenceType().isCall():
                        callee = fm.getFunctionAt(target)
                        callees.append({'callsite':str(a),'addr':str(target),
                                        'name':str(callee.getName()) if callee else None,'ref_type':str(ref.getReferenceType())})
                    if not ref.getReferenceType().isData():
                        continue
                    data = listing.getDataContaining(target)
                    symbol = sm.getPrimarySymbol(target)
                    data_refs.append({'from':str(a),'to':str(target),'ref_type':str(ref.getReferenceType()),
                        'symbol':str(symbol.getName()) if symbol else None,
                        'data_type':str(data.getDataType()) if data else None,
                        'data_value':str(data.getValue()) if data else None})
            callers=[]
            for ref in rm.getReferencesTo(fn.getEntryPoint()):
                owner = fm.getFunctionContaining(ref.getFromAddress())
                callers.append({'callsite':str(ref.getFromAddress()),'addr':str(owner.getEntryPoint()) if owner else None,
                    'name':str(owner.getName()) if owner else None,'ref_type':str(ref.getReferenceType())})
            status={'completed':success,'error':str(result.getErrorMessage()),'seconds':time.monotonic()-started,
                    'timeout_seconds':180,'max_payload_mb':128,'max_instructions':1000000}
            row={'address':va,'name':str(fn.getName()),'signature':str(fn.getSignature()),
                 'calling_convention':str(fn.getCallingConventionName()),'return_type':str(fn.getReturnType()),
                 'parameter_count':int(fn.getParameterCount()),'is_thunk':bool(fn.isThunk()),
                 'body_ranges':[[str(r.getMinAddress()),str(r.getMaxAddress())] for r in fn.getBody().getAddressRanges()],
                 'decompiled':code,'decompile_status':status,'callers':callers,'callees':callees,
                 'assembly':asm,'assembly_bytes':instructions,'data_refs':data_refs,
                 'pcode':pcode,'cfg':cfg,'pcode_errors':pe,'cfg_errors':ce,'pcode_style':'decompile (optimized high P-code)'}
            write_json(folder/(va+'.json'),row)
            index[va]={'name':row['name'],'address':va,'decompile_status':status,
                       'num_callers':len(callers),'num_callees':len(callees),'num_data_refs':len(data_refs)}
            if n%25==0 or not success or n==len(funcs):
                print(f'Exported {n}/{len(funcs)}; {va}; completed={success}',flush=True)
    finally:
        dec.dispose()
    write_json(folder/'_index.json',index)
    combined = []
    for address in sorted(index):
        row = json.loads((folder/(address+'.json')).read_text(encoding='utf-8'))
        combined.append(f'\n/* ---- {row["name"]} @ {address} ---- */\n'+row['decompiled'])
    (output/'lf2_recovered.c').write_text('// Ghidra pseudocode; reference only, not original source.\n'+''.join(combined),encoding='utf-8')
    export_structs(program.getDataTypeManager(),str(folder))
    export_strings(listing,rm,fm,str(folder))
    export_globals(sm,listing,rm,fm,mem,str(folder))
    export_vtables(program,fm,sm,mem,str(folder))
    return index


def supplement(program, root, output):
    """Recover context-dependent SEH fragments separately, rolling back each.

    Tables are verified in this binary: 44b708 is pushed at 4452a1; 44b730
    at 445670; 44b770 at 445915. These are not ordinary callable functions.
    """
    import pefile
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.app.decompiler import DecompInterface
    from ghidra.util.task import TaskMonitor
    pe = pefile.PE(str(root/'reference/decomp/lf2.exe'))
    table_entries = [(0x44b71c,0x445465),(0x44b720,0x445469),(0x44b728,0x445419),(0x44b72c,0x44542d),
                     (0x44b744,0x44568b),(0x44b748,0x4456a7),(0x44b784,0x445999),(0x44b788,0x4459ad)]
    folder = output/'seh_fragments'
    folder.mkdir(exist_ok=True)
    results = []
    for source,target in table_entries:
        assert struct.unpack('<I',pe.get_data(source-pe.OPTIONAL_HEADER.ImageBase,4))[0]==target
        record = {'table_pointer':f'{source:08x}','entry':f'{target:08x}',
            'context':'SEH filter/handler uses parent EBP; temporary function boundary, not a normal function prototype'}
        tx=program.startTransaction('Temporary contextual SEH fragment')
        dec=None
        try:
            a=program.getAddressFactory().getDefaultAddressSpace().getAddress(target)
            record['disassembled']=bool(DisassembleCommand(a,None,True).applyTo(program,TaskMonitor.DUMMY))
            cmd=CreateFunctionCmd(a)
            record['created']=bool(cmd.applyTo(program,TaskMonitor.DUMMY))
            fn=program.getFunctionManager().getFunctionAt(a)
            if fn is not None:
                dec=DecompInterface()
                dec.openProgram(program)
                result=dec.decompileFunction(fn,30,TaskMonitor.DUMMY)
                record.update(completed=bool(result.decompileCompleted()),error=str(result.getErrorMessage()))
                record['assembly']=[{'address':str(i.getAddress()),'bytes':bytes(int(v)&255 for v in i.getBytes()).hex(),
                    'instruction':str(i)} for i in program.getListing().getInstructions(fn.getBody(),True)]
                if result.decompileCompleted():
                    code=str(result.getDecompiledFunction().getC())
                    (folder/(f'{target:08x}.c')).write_text('// CONTEXTUAL SEH FRAGMENT: parent-frame EBP required.\n'+code,encoding='utf-8')
            results.append(record)
        finally:
            if dec:
                dec.dispose()
            program.endTransaction(tx,False)
    write_json(folder/'_index.json',results)
    print('Supplemental SEH fragments:',len(results),flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--install', default='C:/Ghidra/ghidra_12.1.3_PUBLIC', type=Path)
    parser.add_argument('--stage', default='baseline')
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--enhance', action='store_true')
    parser.add_argument('--export', action='store_true')
    parser.add_argument('--export-only', nargs='+')
    parser.add_argument('--fix-callback-conventions', action='store_true')
    parser.add_argument('--supplement', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    copied = output / 'project'
    if not copied.exists():
        shutil.copytree(root / 'reference/ghidra', copied)
        write_json(output/'preserved_inputs.json', {str(p.relative_to(root)):digest(p) for p in
            [root/'reference/decomp/lf2.exe',root/'reference/decomp/lf2_decomp.c',
             *(root/'.ghidra-exports').glob('*.json'),*(root/'reference/ghidra').rglob('*.gbf')]})
    stage = output / args.stage
    stage.mkdir(exist_ok=True)
    import pyghidra
    print('Starting Ghidra; isolated project:',copied,flush=True)
    pyghidra.start(install_dir=args.install)
    project = pyghidra.open_project(copied,'lf2')
    try:
        with pyghidra.program_context(project, '/lf2.exe') as program:
            if str(program.getExecutableSHA256()).lower() != digest(root/'reference/decomp/lf2.exe'):
                raise ValueError('Ghidra program and reference executable have different SHA256 values')
            if args.probe:
                probe(program,stage)
            else:
                if args.enhance:
                    enhance(program,root,stage)
                if args.fix_callback_conventions:
                    from ghidra.util.task import TaskMonitor
                    changes=[]
                    with pyghidra.transaction(program,'Set callback conventions proven by RET operands'):
                        for va,pop_bytes in [(0x43b3d0,16),(0x43c240,4)]:
                            fn=program.getFunctionManager().getFunctionAt(program.getAddressFactory().getDefaultAddressSpace().getAddress(va))
                            returns=[str(i) for i in program.getListing().getInstructions(fn.getBody(),True)
                                     if str(i.getMnemonicString())=='RET']
                            assert returns and all(s=='RET '+hex(pop_bytes) for s in returns)
                            fn.setCallingConvention('__stdcall')
                            changes.append({'address':str(fn.getEntryPoint()),'convention':'__stdcall','returns':returns})
                    program.save('Verified callback conventions',TaskMonitor.DUMMY)
                    write_json(stage/'callback_conventions.json',changes)
                audit(program, root, stage)
                if args.export:
                    export_rich(program,stage,args.export_only)
                if args.supplement:
                    supplement(program,root,stage)
    finally:
        project.close()


if __name__ == '__main__':
    main()
