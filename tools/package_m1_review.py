"""Freeze the local M1 review evidence without Git, downloads or external writes.

Run from the existing WSL environment after checks. The package contains local
proprietary evidence and is not uploaded anywhere.
"""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import struct
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
out = root / 'reference/round-m1-2026-09-10'
before = out / 'before'
final = out / 'final'
final.mkdir(exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

# Verify the untouched baseline bytes, including the user's initial main/RNG.
original = json.loads((out/'before-hashes.json').read_text(encoding='utf-8-sig'))
for entry in original:
    rel = entry['Path'].replace('\\', '/').split('/before/', 1)[1]
    assert sha(before/rel).upper() == entry['Hash'], rel
for rel in ('src/engine/random.hpp', 'tests/test_random.cpp'):
    assert sha(root/rel) == sha(before/rel), 'pre-existing work changed: '+rel

source = sorted(list((root/'src').rglob('*.hpp')) + list((root/'src').rglob('*.cpp'))
                + list((root/'tests').glob('*.cpp')))
extras = ['Makefile.host', 'CMakeLists.txt', 'build-vita.sh',
          'tools/run_m1_checks.py', 'tools/package_m1_review.py',
          'docs/auditorias/AUDITORIA_2026-09-10_M1.md', 'docs/PROTOCOLO.md',
          'docs/PLANO_FIDELIDADE_E_EXECUCAO.md', 'docs/prompts/PROMPT_PILOTO_FIDELIDADE.md',
          'docs/auditorias/AUDITORIA_2026-08-12.md', 'docs/auditorias/AUDITORIA_SUPERFICIE.md',
          'docs/sessoes/RELATORIO_2026-08-12.md']
source += [root/p for p in extras]
for p in source:
    dest = final/p.relative_to(root)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, dest)

# Diff against the actual pre-round files, never against HEAD (already dirty).
# CMake's initial copy was not captured: full final file plus explicit change
# description in the audit, rather than a fabricated baseline.
patch = []
changed = []
for p in source:
    rel = p.relative_to(root).as_posix()
    old = before/rel
    is_code = rel.startswith(('src/', 'tests/', 'tools/')) or rel=='Makefile.host'
    if not is_code:
        continue
    if old.exists() and sha(old)==sha(p):
        continue
    changed.append(rel)
    patch.extend(difflib.unified_diff(
        old.read_text().splitlines(True) if old.exists() else [],
        p.read_text().splitlines(True), fromfile='before/'+rel if old.exists() else '/dev/null',
        tofile='final/'+rel))
(out/'round.patch').write_text(''.join(patch))
(out/'changed-files.json').write_text(json.dumps(changed+['CMakeLists.txt',
    'docs/auditorias/AUDITORIA_2026-09-10_M1.md', 'reference/round-m1-2026-09-10/'], indent=2)+'\n')

# Decode constants using the executable's PE section table, not VA subtraction
# guessed from an export. Keep offsets and raw bytes alongside decoded doubles.
exe = root/'reference/decomp/lf2.exe'
b = exe.read_bytes()
pe = struct.unpack_from('<I', b, 0x3c)[0]
assert b[pe:pe+4] == b'PE\0\0'
sections = struct.unpack_from('<H',b,pe+6)[0]
opt_size = struct.unpack_from('<H',b,pe+20)[0]
opt = pe+24
assert struct.unpack_from('<H',b,opt)[0]==0x10b
base = struct.unpack_from('<I',b,opt+28)[0]
table = []
for i in range(sections):
    s=opt+opt_size+i*40
    vs, va, rs, rp = struct.unpack_from('<IIII',b,s+8)
    table.append((va,rs,rp))
constants=[]
for va, expected in [(0x449048,5), (0x448308,-5), (0x449050,-3),
                     (0x449058,-4), (0x4479e0,2), (0x447a50,7),
                     (0x448338,12), (0x448340,-8), (0x447980,8)]:
    rva=va-base
    offset=next(rp+rva-sva for sva,rs,rp in table if sva<=rva and rva+8<=sva+rs)
    raw=b[offset:offset+8]
    value=struct.unpack('<d',raw)[0]
    assert value==expected, (hex(va),value,expected)
    constants.append(dict(va=hex(va),file_offset=hex(offset),bytes=raw.hex(),double=value))
(out/'binary-evidence.json').write_text(json.dumps(dict(path=str(exe.relative_to(root)),
    sha256=sha(exe),size=len(b),image_base=hex(base),constants=constants),indent=2)+'\n')

# Preserve the raw disassembly ranges used in this round with their guards.
ranges=[(0x4196f0,0x4197a0),(0x40d960,0x40de30),(0x40de30,0x40de55),
        (0x40e490,0x40e510),(0x40e780,0x40e890),(0x418960,0x4189b0),
        (0x41a770,0x41a7a4),(0x41f520,0x41f560),(0x41faf8,0x41fb12),
        (0x42e600,0x42e650),(0x42e810,0x42e895),(0x42ea65,0x42fdc6),
        (0x43008b,0x4303b0),(0x4450d0,0x445150)]
assembly=(out/'lf2.asm').read_text().splitlines(True)
selected=[]
for lo,hi in ranges:
    selected.append('\n# Range %08x..%08x\n'%(lo,hi))
    for line in assembly:
        head=line.strip().split(':',1)[0]
        try: va=int(head,16)
        except ValueError: continue
        if lo<=va<hi: selected.append(line)
(out/'relevant.asm').write_text(''.join(selected))

with (out/'environment.log').open('w') as log:
    for cmd in [['uname','-a'],['g++','--version'],['make','--version'],
                ['objdump','--version'],['python3','--version'],['cmake','--version'],
                ['/usr/local/vitasdk/bin/arm-vita-eabi-g++','--version']]:
        r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        log.write('$ '+' '.join(cmd)+'\n'+r.stdout+'EXIT='+str(r.returncode)+'\n')
        assert r.returncode==0,cmd

shutil.copy2(root/'lf2vita.log', out/'final-lf2vita.log')
shutil.copy2(root/'build/lf2-vita.vpk', out/'final-lf2-vita.vpk')
with zipfile.ZipFile(out/'final-lf2-vita.vpk') as vpk:
    assert vpk.testzip() is None
    names=vpk.namelist()
    assert 'eboot.bin' in names and 'sce_sys/param.sfo' in names
    (out/'vpk-inspection.json').write_text(json.dumps(dict(
        sha256=sha(out/'final-lf2-vita.vpk'), size=(out/'final-lf2-vita.vpk').stat().st_size,
        entries=len(names), crc_check='all entries OK',
        eboot_size=vpk.getinfo('eboot.bin').file_size,
        note='Archive inspection and cross-build only; no Vita execution.'),indent=2)+'\n')

files=sorted(p for p in final.rglob('*') if p.is_file())
manifest={p.relative_to(final).as_posix():sha(p) for p in files}
(out/'final-source-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
version=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
(out/'VERSION.txt').write_text('M1 source manifest SHA256: '+version+'\n'
    'No Git write. Baseline is the preserved dirty working tree.\n'
    'Status: verification incomplete; see audit and REVISAO.md.\n')
print('Baseline hashes preserved:',len(original))
print('Final source files:',len(files),'Manifest identity:',version)
print('PE constants verified:',len(constants),'VPK entries:',len(names))

package=out/'PACOTE_REVISAO_M1.zip'
with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(out.rglob('*')):
        if p.is_file() and p!=package and p.name not in ('package.log','package-sha256.txt'):
            z.write(p,p.relative_to(out))
    z.write(exe,'primary/lf2.exe')
with zipfile.ZipFile(package) as z:
    assert z.testzip() is None
    assert 'REVISAO.md' in z.namelist() and 'VERSION.txt' in z.namelist()
(out/'package-sha256.txt').write_text(sha(package)+'  '+package.name+'\n')
print('Local review archive:',package.name,'bytes:',package.stat().st_size,'CRC: OK')
