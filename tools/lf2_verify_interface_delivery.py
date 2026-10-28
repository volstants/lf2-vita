"""Check the built Vita package and save a source manifest/diff without Git."""
from pathlib import Path
import ast,difflib,hashlib,json,zipfile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'reference/round-interface-2026-09-22'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
baseline=json.loads((OUT/'baseline-sha256.json').read_text())
source=[*ROOT.joinpath('src').rglob('*.hpp'),ROOT/'src/main.cpp']
# Keep the historical source-map generator usable with this larger source tree.
module=ast.parse((ROOT/'tools/lf2_pair_port.py').read_text(encoding='utf-8'))
filemap=next(ast.literal_eval(n.value) for n in module.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='FILES' for t in n.targets))
assert set(filemap)=={p.relative_to(ROOT).as_posix() for p in source}
files=set(baseline)|{p.relative_to(ROOT).as_posix() for p in source}
files.update(['tests/test_frontend.cpp','tools/lf2_prepare_backgrounds.py','tools/lf2_frontend_stress.py',
              'tools/lf2_verify_interface_delivery.py','docs/auditorias/AUDITORIA_2026-09-22_INTERFACE.md'])
patch=[];changes=[]
for relative in sorted(files):
    p=ROOT/relative;old=OUT/'before'/relative
    if old.exists():assert sha(old)==baseline[relative]
    if old.exists() and old.read_bytes()==p.read_bytes():continue
    changes.append(dict(file=relative,sha256=sha(p),before_sha256=baseline.get(relative)))
    a=old.read_text(encoding='utf-8').splitlines(keepends=True) if old.exists() else []
    b=p.read_text(encoding='utf-8').splitlines(keepends=True)
    patch.extend(difflib.unified_diff(a,b,fromfile='before/'+relative if old.exists() else '/dev/null',tofile='after/'+relative))
(OUT/'changes.patch').write_text(''.join(patch),encoding='utf-8')
records=json.loads((OUT/'background-assets.json').read_text())
for item in records:assert sha(ROOT/item['png'])==item['sha256']
# Verify ALL entries against the current source/build, not just ZIP integrity.
vpk=ROOT/'build/lf2-vita.vpk'
with zipfile.ZipFile(vpk) as z:
    assert z.testzip() is None
    names=z.namelist();assert len(names)==len(set(names))
    for name in names:
        if name.endswith('/'):continue
        local=ROOT/({'eboot.bin':'build/lf2-vita.self','sce_sys/param.sfo':'build/lf2-vita.vpk_param.sfo'}.get(name,name))
        assert local.is_file(),name
        assert z.read(name)==local.read_bytes(),name
    for item in records:assert item['png'] in names
    for p in (ROOT/'assets').glob('*.png'):assert p.relative_to(ROOT).as_posix() in names
stress=json.loads((OUT/'stress-results.json').read_text())
assert len(stress)==35 and all(item['passed'] for item in stress)
result={'passed':True,'vpk_sha256':sha(vpk),'vpk_bytes':vpk.stat().st_size,'vpk_entries':len(names),
        'original_exe_sha256':sha(ROOT/'reference/decomp/lf2.exe'),
        'stress_cases':len(stress),'stress_ticks':sum(x['ticks'] for x in stress),
        'background_pngs':len(records),'changes':changes}
(OUT/'delivery-verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='changes'},indent=2))
