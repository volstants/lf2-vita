"""Verify exported bytes and preserved inputs; classify all undefined .text gaps.

The classifications and byte coverage are not claims of semantic equivalence.
Run using the project venv: python tools/lf2_verify_ghidra_audit.py OUTPUT_DIR.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import sys

import capstone
import pefile


def main():
    root=Path(__file__).resolve().parent.parent
    output=Path(sys.argv[1]).resolve()
    stage=output/'enhanced'
    pe=pefile.PE(str(root/'reference/decomp/lf2.exe'))
    base=pe.OPTIONAL_HEADER.ImageBase
    cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    cs.detail=True
    def load(path):
        return json.loads(path.read_text(encoding='utf-8'))
    def save(path,obj):
        path.write_text(json.dumps(obj,indent=2,ensure_ascii=False),encoding='utf-8')
    preserved=load(output/'preserved_inputs.json')
    changed=[p for p,h in preserved.items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
    assert not changed, ('Original inputs changed',changed)
    baseline={f['address']:f for f in load(output/'baseline/functions.json')}
    current={f['address']:f for f in load(stage/'functions.json')}
    assert all(baseline[a]['body']==current[a]['body'] for a in baseline), 'Original function boundaries changed'
    exports={p.stem:load(p) for p in (stage/'exports').glob('[0-9]*.json')}
    assert exports.keys()==current.keys(), 'Exported function set differs from database inventory'
    errors=[]
    covered=set()
    total_refs=0
    failed=[]
    def check_instruction(row):
        a=int(row['address'],16)
        data=bytes.fromhex(row['bytes'])
        if pe.get_data(a-base,len(data))!=data:
            errors.append({'address':row['address'],'reason':'bytes differ'})
        decoded=list(cs.disasm(data,a,count=1))
        if not decoded or decoded[0].size!=len(data):
            errors.append({'address':row['address'],'reason':'instruction length differs'})
    for address,f in exports.items():
        if not f['decompile_status']['completed']:
            failed.append(address)
        assert not f['pcode_errors'] and not f['cfg_errors'], address
        total_refs+=len(f['data_refs'])
        for instruction in f['assembly_bytes']:
            check_instruction(instruction)
            a=int(instruction['address'],16)
            covered.update(range(a,a+len(bytes.fromhex(instruction['bytes']))))
    assert not failed, ('Decompilation failed',failed)
    seh=load(stage/'seh_fragments/_index.json')
    assert all(s.get('completed') for s in seh), 'Supplemental decompilation failed'
    seh_instruction_bytes=set()
    for fragment in seh:
        for instruction in fragment['assembly']:
            check_instruction(instruction)
            a=int(instruction['address'],16)
            seh_instruction_bytes.update(range(a,a+len(bytes.fromhex(instruction['bytes']))))
    assert not errors,errors
    rows=load(stage/'undefined_ranges.json')
    stats=collections.Counter()
    virtual_end=base+next(s.VirtualAddress+s.Misc_VirtualSize for s in pe.sections if s.Name.rstrip(b'\0')==b'.text')
    def is_nop(i):
        if i.mnemonic in ('int3','nop'):
            return True
        ops=i.operands
        if i.mnemonic in ('mov','xchg') and len(ops)==2 and ops[0].type==ops[1].type==capstone.x86.X86_OP_REG:
            return ops[0].reg==ops[1].reg
        if i.mnemonic=='lea' and len(ops)==2 and ops[0].type==capstone.x86.X86_OP_REG and ops[1].type==capstone.x86.X86_OP_MEM:
            return ops[1].mem.base==ops[0].reg and ops[1].mem.index==0 and ops[1].mem.disp==0
        return False
    undefined=set()
    for row in rows:
        a=int(row['start'],16)
        data=pe.get_data(a-base,row['length'])
        undefined.update(range(a,a+len(data)))
        instructions=list(cs.disasm(data,a))
        row['assembly']=[{'address':f'{i.address:08x}','bytes':i.bytes.hex(),'instruction':i.mnemonic+' '+i.op_str} for i in instructions]
        if a>=virtual_end and not any(data):
            kind='raw_section_tail_zero_fill'
        elif data.rstrip(b'\xcc').startswith((b'\r\nGVMat32',b'\r\nasm686')):
            kind='embedded_ascii_credit_and_padding'
        elif sum(i.size for i in instructions)==len(data) and all(is_nop(i) or
                (i.mnemonic=='jmp' and i.operands[0].type==capstone.x86.X86_OP_IMM and i.operands[0].imm>=a+len(data)) for i in instructions):
            kind='padding_or_jump_over_padding_candidate'
        else:
            kind='code_or_unresolved'
        row['classification']=kind
        stats[kind]+=row['length']
    save(stage/'gap_review.json',{'classification_bytes':dict(stats),'ranges':rows})
    summary=load(stage/'summary.json')
    assert not summary['ghidra_disk_instruction_byte_mismatches']
    assert not summary['capstone_instruction_length_disagreements']
    assert not summary['missing_direct_call_targets']
    assert len(covered)==summary['function_body_bytes']
    combined=(stage/'lf2_recovered.c').read_text(encoding='utf-8')
    headers=re.findall(r'^/\* ---- .*? @ ([0-9a-f]+) ---- \*/',combined,re.M)
    assert len(headers)==len(exports) and set(headers)==set(exports)
    result={'passed':True,'original_input_files_hash_verified':len(preserved),
        'original_function_bodies_unchanged':len(baseline),'exported_functions':len(exports),
        'decompilation_failures':failed,'byte_or_decode_errors':errors,
        'unique_exported_instruction_bytes':len(covered),'data_references':total_refs,
        'functions_with_data_references':sum(bool(f['data_refs']) for f in exports.values()),
        'new_function_addresses':sorted(current.keys()-baseline.keys()),
        'new_vs_historical_c':summary['current_only_vs_old_c'],
        'supplemental_seh_fragments':len(seh),
        'previously_undefined_bytes_in_seh_supplement':len(seh_instruction_bytes & undefined),
        'gap_classification_bytes':dict(stats),'c_file_characters':len(combined),
        'c_lines':len(combined.splitlines())}
    save(output/'verification.json',result)
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
