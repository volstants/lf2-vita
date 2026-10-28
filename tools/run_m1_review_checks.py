"""Local checks for the M1 review response; preserves the first delivery."""
from pathlib import Path
import argparse, os, subprocess, sys
root=Path(__file__).resolve().parents[1]
out=root/'reference/round-m1-review-fixes-2026-09-11'
p=argparse.ArgumentParser()
p.add_argument('label')
p.add_argument('mode',choices=['phases','contact','old-gap','suite','integration','main','harness','vita'])
p.add_argument('--before',action='store_true')
a=p.parse_args()
inc='reference/round-m1-review-fixes-2026-09-11/before/src' if a.before else 'src'
suffix='-before' if a.before else ''
cmds={
 'phases':[['g++','-std=c++17','-O2','-Wall','-Wextra','-I',inc,'tests/test_hit_phases.cpp','-o','bin/test_hit_phases'+suffix],['bin/test_hit_phases'+suffix]],
 'contact':[['g++','-std=c++17','-O2','-Wall','-Wextra','-I',inc,'tests/test_hit_contact.cpp','-o','bin/test_hit_contact'+suffix],['bin/test_hit_contact'+suffix]],
 'old-gap':[['g++','-std=c++17','-O2','-Wall','-Wextra','-I',inc,'reference/round-m1-2026-09-10/phase-gap.cpp','-o','bin/test_m1_phase_gap'+suffix],['bin/test_m1_phase_gap'+suffix]],
 'suite':[['make','-B','-f','Makefile.host','test']],
 'integration':[['make','-f','Makefile.host','test-hit-integration']],
 'main':[['make','-f','Makefile.host','check-main']],
 'harness':[['make','-f','Makefile.host','harness','TICKS=9000']],
 'vita':[['bash','build-vita.sh']],
}[a.mode]
if a.mode in ('integration','main','harness'):
 for f in ('.sdlhost/root/usr/include/SDL2','.sdlhost/lib/libSDL2-2.0.so.0','.sdlhost/lib/libSDL2_image-2.0.so.0'):
  if not (root/f).exists(): sys.exit('Missing SDL; no installer fallback: '+f)
env=os.environ.copy()
if a.mode=='vita': env['VITASDK']='/usr/local/vitasdk'
with (out/(a.label+'.log')).open('w') as log:
 for cmd in cmds:
  r=subprocess.run(cmd,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  text='$ '+' '.join(cmd)+'\n'+r.stdout+'EXIT='+str(r.returncode)+'\n'
  log.write(text); log.flush(); print(text,flush=True)
  if r.returncode: sys.exit(r.returncode)
