"""Run the already-built host harness with explicit argv and record exit codes."""
from pathlib import Path
import json
import os
import subprocess

root=Path(__file__).resolve().parents[1]
out=root/"reference/round-pairing-2026-09-21"
results=[]
for name,index in (("dennis",0),("rudolf",5),("henry",7)):
    executable=["./bin/lf2-headless","9000",str(index)]
    if os.name=="nt":
        path="/mnt/"+root.drive[0].lower()+root.as_posix()[2:]
        cmd=["wsl","-d","Ubuntu","--cd",path,"--","env",
             "SDL_VIDEODRIVER=dummy","LD_LIBRARY_PATH=.sdlhost/lib:/lib/x86_64-linux-gnu",*executable]
        env=None
    else:
        cmd=executable
        env=dict(os.environ,SDL_VIDEODRIVER="dummy",
                 LD_LIBRARY_PATH=".sdlhost/lib:/lib/x86_64-linux-gnu")
    logfile=out/f"harness-verified-{name}.log"
    with logfile.open("wb") as stream:
        process=subprocess.run(cmd,cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT)
    text=logfile.read_text(encoding="utf-8",errors="replace")
    ok=process.returncode==0 and "resultado            : ok" in text
    results.append({"character":name,"ticks":9000,"exit_code":process.returncode,
                    "passed":ok,"log":logfile.name})
    print(name,process.returncode,"passed" if ok else "FAILED",flush=True)
(out/"harness-verification.json").write_text(json.dumps(results,indent=2)+"\n")
raise SystemExit(0 if all(r["passed"] for r in results) else 1)
