"""Inspect LF2 PE imports, local DLL candidates and historical Ghidra exports.

Static inspection only. System DLL candidates are not asserted to be the DLLs
used in 2009 or selected by the Windows loader for a live LF2 process.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re

import pefile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    args.output.mkdir(parents=True, exist_ok=True)
    exe = root/'reference/decomp/lf2.exe'
    pe = pefile.PE(str(exe))
    windows = Path(os.environ.get('WINDIR', 'C:/Windows'))
    crt_dirs = list((windows/'WinSxS').glob('x86_microsoft.vc80.crt_*'))
    search_dirs = [exe.parent,Path('C:/Program Files (x86)/LittleFighter'),windows/'SysWOW64',*crt_dirs]
    libraries = []
    for imp in pe.DIRECTORY_ENTRY_IMPORT:
        name = imp.dll.decode()
        row = {'dll':name,'import_count':len(imp.imports),'candidates':[]}
        for directory in search_dirs:
            path = directory/name
            if not path.exists():
                continue
            dll = pefile.PE(str(path))
            by_name = {e.name.decode():e for e in getattr(dll,'DIRECTORY_ENTRY_EXPORT',type('E',(),{'symbols':[]})()).symbols if e.name}
            by_ord = {e.ordinal:e for e in getattr(dll,'DIRECTORY_ENTRY_EXPORT',type('E',(),{'symbols':[]})()).symbols}
            matches = []
            for symbol in imp.imports:
                wanted = symbol.name.decode() if symbol.name else symbol.ordinal
                found = by_name.get(wanted) if symbol.name else by_ord.get(wanted)
                matches.append({'import':wanted,'found':found is not None,
                    'rva':hex(found.address) if found else None,
                    'forwarder':found.forwarder.decode() if found and found.forwarder else None})
            row['candidates'].append({'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                'machine':hex(dll.FILE_HEADER.Machine),'matches':matches})
            dll.close()
        libraries.append(row)
    manifests = []
    if hasattr(pe,'DIRECTORY_ENTRY_RESOURCE'):
        for typ in pe.DIRECTORY_ENTRY_RESOURCE.entries:
            if typ.id == 24:
                for name in typ.directory.entries:
                    for lang in name.directory.entries:
                        item = lang.data.struct
                        manifests.append(pe.get_data(item.OffsetToData,item.Size).decode('utf-8',errors='replace'))
    funcs = [json.loads(p.read_text(encoding='utf-8')) for p in (root/'.ghidra-exports').glob('[0-9]*.json')]
    warnings = collections.Counter(w for x in funcs for w in re.findall(r'/\* WARNING:.*?\*/',x['decompiled'],re.S))
    result = {'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'imports_total':sum(len(i.imports) for i in pe.DIRECTORY_ENTRY_IMPORT),
        'libraries':libraries,'embedded_manifests':manifests,
        'exports':{'count':len(funcs),
            'failed':[x['address'] for x in funcs if x['decompiled'].startswith('//')],
            'empty_data_refs':sum(not x['data_refs'] for x in funcs),
            'pcode_errors':{x['address']:x['pcode_errors'] for x in funcs if x['pcode_errors']},
            'cfg_errors':{x['address']:x['cfg_errors'] for x in funcs if x['cfg_errors']},
            'warning_counts':dict(warnings)}}
    (args.output/'binary_inventory.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
    print('Imported libraries:',len(libraries),'symbols:',result['imports_total'])
    for row in libraries:
        print(row['dll'],row['import_count'],[(c['machine'],sum(m['found'] for m in c['matches']),c['path']) for c in row['candidates']])
    print('Embedded manifests:',manifests)
    print('Failed exports:',result['exports']['failed'])


if __name__ == '__main__':
    main()
