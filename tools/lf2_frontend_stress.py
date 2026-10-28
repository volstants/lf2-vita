"""Exercise the compiled real game loop with the full roster and 1-7 CPUs."""
from pathlib import Path
import json,os,re,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'reference/round-interface-2026-09-22'
names=['dennis','davis','woody','firen','freeze','rudolf','louis','henry','deep','john','bandit','hunter','mark','jack','sorcerer','monk','jan','knight','justin','bat','louisEX','firzen','julian']
cases=[(name,1800,i,3,1,'audit') for i,name in enumerate(names)]
cases += [('eight-'+names[i],9000,i,7,1,'audit') for i in (0,5,7)]
cases += [('scene-'+str(bg),1200,8,1+bg%7,bg,'normal') for bg in range(9)]
results=[]
for name,ticks,index,cpus,bg,mode in cases:
    exe=['./bin/lf2-headless',str(ticks),str(index),str(cpus),str(bg),mode]
    if os.name=='nt':
        path='/mnt/'+root.drive[0].lower()+root.as_posix()[2:]
        cmd=['wsl','-d','Ubuntu','--cd',path,'--','env','SDL_VIDEODRIVER=dummy','LD_LIBRARY_PATH=.sdlhost/lib:/lib/x86_64-linux-gnu',*exe]
        env=None
    else:
        cmd=exe;env=dict(os.environ,SDL_VIDEODRIVER='dummy',LD_LIBRARY_PATH='.sdlhost/lib:/lib/x86_64-linux-gnu')
    with (out/('stress-'+name+'.log')).open('wb') as stream:
        proc=subprocess.run(cmd,cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT)
    log=(out/('stress-'+name+'.log')).read_text(encoding='utf-8',errors='replace')
    passed=proc.returncode==0 and 'resultado            : ok' in log and '[ERRO]' not in log
    results.append(dict(case=name,ticks=ticks,cpus=cpus,background=bg,mode=mode,exit_code=proc.returncode,passed=passed))
    print(name,'PASS' if passed else 'FAIL',flush=True)
    (out/'stress-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
