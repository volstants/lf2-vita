"""Freeze the response to the M1 review. No Git, installation or upload."""
from pathlib import Path
import difflib,hashlib,json,shutil,subprocess,zipfile
root=Path(__file__).resolve().parents[1]
out=root/'reference/round-m1-review-fixes-2026-09-11'
old=root/'reference/round-m1-2026-09-10'
before=out/'before'; final=out/'final'; final.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
baseline=json.loads((out/'before-hashes.json').read_text(encoding='utf-8-sig'))
for e in baseline:
 rel=e['Path'].replace('\\','/').split('/before/',1)[1]
 assert sha(before/rel).upper()==e['Hash'],rel
previous=json.loads((old/'final-source-sha256.json').read_text())
deviations=[rel for rel,digest in previous.items() if (before/rel).is_file() and sha(before/rel)!=digest]
(out/'baseline-vs-first-delivery.json').write_text(json.dumps(deviations,indent=2)+'\n')
oldzip=old/'PACOTE_REVISAO_M1.zip'
assert sha(oldzip)==(old/'package-sha256.txt').read_text().split()[0]
for rel in ['src/engine/random.hpp','tests/test_random.cpp']:
 assert sha(root/rel)==sha(before/rel),rel
files=sorted(list((root/'src').rglob('*.hpp'))+list((root/'src').rglob('*.cpp'))+list((root/'tests').glob('*.cpp')))
files += [root/p for p in ['Makefile.host','CMakeLists.txt','build-vita.sh','PROTOCOLO.md',
 'PLANO_FIDELIDADE_E_EXECUCAO.md','PROMPT_PILOTO_FIDELIDADE.md','AUDITORIA_2026-09-10_M1.md',
 'AUDITORIA_2026-09-11_M1_REVISAO.md','tools/run_m1_review_checks.py',
 'tools/capture_m1_review_evidence.py','tools/package_m1_review_fixes.py']]
manifest={}; diff=[]; changed=[]
for p in files:
 rel=p.relative_to(root).as_posix(); dest=final/rel
 dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(p,dest)
 manifest[rel]=sha(p)
 if rel.startswith(('src/','tests/','tools/')) or rel in ('Makefile.host','CMakeLists.txt','build-vita.sh'):
  b=before/rel
  if b.exists() and sha(b)==sha(p): continue
  changed.append(rel)
  diff.extend(difflib.unified_diff(b.read_text().splitlines(True) if b.exists() else [],
   p.read_text().splitlines(True),fromfile='before/'+rel if b.exists() else '/dev/null',tofile='final/'+rel))
(out/'round.patch').write_text(''.join(diff))
(out/'changed-files.json').write_text(json.dumps(changed,indent=2)+'\n')
(out/'final-source-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
version=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
(out/'VERSION.txt').write_text('M1 review fixes source manifest SHA256: '+version+'\nNo Git writes.\n')
shutil.copy2(root/'lf2vita.log',out/'final-lf2vita.log')
shutil.copy2(root/'build/lf2-vita.vpk',out/'final-lf2-vita.vpk')
with zipfile.ZipFile(out/'final-lf2-vita.vpk') as z:
 assert z.testzip() is None
 assert 'eboot.bin' in z.namelist() and 'sce_sys/param.sfo' in z.namelist()
 (out/'vpk-inspection.json').write_text(json.dumps(dict(sha256=sha(out/'final-lf2-vita.vpk'),
  entries=len(z.namelist()),eboot_size=z.getinfo('eboot.bin').file_size,crc='OK',device_test=False),indent=2)+'\n')
with (out/'environment.log').open('w') as log:
 for cmd in [['uname','-a'],['g++','--version'],['make','--version'],['objdump','--version'],
             ['python3','--version'],['cmake','--version'],['/usr/local/vitasdk/bin/arm-vita-eabi-g++','--version']]:
  r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  log.write('$ '+' '.join(cmd)+'\n'+r.stdout+'EXIT='+str(r.returncode)+'\n'); assert r.returncode==0
package=out/'PACOTE_NOVA_REVISAO_M1.zip'
with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(out.rglob('*')):
  if p.is_file() and p!=package and p.name not in ('package-sha256.txt','package.log'):
   z.write(p,p.relative_to(out))
 z.write(root/'reference/decomp/lf2.exe','primary/lf2.exe')
 for name in ['phase-gap.cpp','REVISAO.md','VERSION.txt','open-phase-gap.log']:
  z.write(old/name,'first-delivery/'+name)
with zipfile.ZipFile(package) as z:
 assert z.testzip() is None
 assert 'REVISAO.md' in z.namelist() and 'final-source-sha256.json' in z.namelist()
(out/'package-sha256.txt').write_text(sha(package)+'  '+package.name+'\n')
print('Baseline hashes checked:',len(baseline),'Differences from first delivery:',deviations)
print('Old review package unchanged. RNG source/test unchanged.')
print('Final source files:',len(manifest),'Changed:',changed)
print('Source identity:',version)
print('Review archive:',package.name,'bytes:',package.stat().st_size,'CRC: OK')
