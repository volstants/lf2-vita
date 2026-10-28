"""Read the existing PE and disassemble the exact maintenance/contact ranges."""
from pathlib import Path
import hashlib,json,struct,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'reference/round-m1-review-fixes-2026-09-11'
exe=root/'reference/decomp/lf2.exe'
b=exe.read_bytes(); pe=struct.unpack_from('<I',b,0x3c)[0]; opt=pe+24
assert b[pe:pe+4]==b'PE\0\0'
base=struct.unpack_from('<I',b,opt+28)[0]
n=struct.unpack_from('<H',b,pe+6)[0]; size=struct.unpack_from('<H',b,pe+20)[0]
sects=[struct.unpack_from('<IIII',b,opt+size+i*40+8) for i in range(n)]
constants=[]
for va in [0x448388,0x448380,0x448378,0x448370,0x448348,0x448328,0x448320,0x448330,0x447a40,0x447a50,0x448338,0x449050,0x448910]:
 rva=va-base; off=next(rp+rva-sva for vs,sva,rs,rp in sects if sva<=rva and rva+8<=sva+rs)
 raw=b[off:off+8]
 constants.append(dict(va=hex(va),offset=hex(off),bytes=raw.hex(),double=struct.unpack('<d',raw)[0]))
(out/'binary-evidence.json').write_text(json.dumps(dict(sha256=hashlib.sha256(b).hexdigest(),constants=constants),indent=2)+'\n')
for name,lo,hi in [('maintenance',0x40d960,0x40de23),('motion-contact',0x40e490,0x40ef6b),('commit',0x4196f0,0x4197a0),('resting-fall',0x41faf8,0x41fc61)]:
 cmd=['objdump','-d','--start-address='+hex(lo),'--stop-address='+hex(hi),str(exe)]
 r=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 assert r.returncode==0
 (out/(name+'.asm')).write_text('$ '+' '.join(cmd)+'\n'+r.stdout)
print(json.dumps(constants,indent=2))
