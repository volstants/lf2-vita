"""Run bounded M1 checks, preserve stdout/stderr and actual child exit codes.
Existing WSL toolchain only; never downloads dependencies.
"""
import argparse
import pathlib
import subprocess
import sys
import os

root = pathlib.Path(__file__).resolve().parents[1]
out = root / 'reference/round-m1-2026-09-10'
p = argparse.ArgumentParser()
p.add_argument('label')
p.add_argument('mode', choices=['pipeline', 'integration', 'suite', 'main', 'harness', 'vita', 'phase-gap'])
p.add_argument('--stage', type=int, default=6)
p.add_argument('--baseline', action='store_true')
a = p.parse_args()
if a.mode in ('main', 'harness', 'integration'):
    for path in ('.sdlhost/root/usr/include/SDL2', '.sdlhost/lib/libSDL2-2.0.so.0',
                 '.sdlhost/lib/libSDL2_image-2.0.so.0'):
        if not (root / path).exists():
            sys.exit('Missing existing SDL dependency; refusing installer fallback: '+path)
commands = {
    'phase-gap': [['g++','-std=c++17','-O2','-Wall','-Wextra','-I','src',
                   'reference/round-m1-2026-09-10/phase-gap.cpp','-o','bin/test_m1_phase_gap'],
                  ['bin/test_m1_phase_gap']],
    'pipeline': [['g++','-std=c++17','-O2','-Wall','-Wextra','-I',
                  'reference/round-m1-2026-09-10/before/src' if a.baseline else 'src',
                  '-DM1_STAGE='+str(a.stage),'tests/test_hit_pipeline.cpp',
                  '-o','bin/test_hit_pipeline'], ['bin/test_hit_pipeline']],
    'suite': [['make','-B','-f','Makefile.host','test']],
    'integration': [['make','-f','Makefile.host','test-hit-integration']],
    'main': [['make','-f','Makefile.host','check-main']],
    'harness': [['make','-f','Makefile.host','harness','TICKS=9000']],
    'vita': [['bash','build-vita.sh']],
}[a.mode]
env=os.environ.copy()
if a.mode=='vita':
    env['VITASDK']='/usr/local/vitasdk' # existing install confirmed this round.
with (out / (a.label+'.log')).open('w') as log:
    for cmd in commands:
        heading = '$ '+' '.join(cmd)+'\n'
        log.write(heading); log.flush()
        result = subprocess.run(cmd, cwd=root, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        log.write(result.stdout)
        log.write('EXIT='+str(result.returncode)+'\n'); log.flush()
        print(heading+result.stdout+'EXIT='+str(result.returncode), flush=True)
        if result.returncode:
            sys.exit(result.returncode)
