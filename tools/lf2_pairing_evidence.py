"""Capture and emulate bounded LF2 instruction ranges; never launch the EXE.

Requires the project venv's pefile, capstone and unicorn. Outputs independent
binary-derived CSV expectations consumed by tests/test_binary_pairing.cpp.
The emulator maps private memory and executes no OS calls or game process.
"""
from pathlib import Path
import csv
import hashlib
import itertools
import json
import struct

import capstone
import pefile
import unicorn
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "reference/round-pairing-2026-09-21"
EXPORT = ROOT / "reference/audit-ghidra-2026-09-20/enhanced/exports"
EXE = ROOT / "reference/decomp/lf2.exe"
SHA = "12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5"


def main():
    raw = EXE.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SHA
    OUT.mkdir(exist_ok=True)
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    ranges = {
        "mp-init": (0x4063CA, 0x4063E7, "004061d0"),
        "mp-regen": (0x41F994, 0x41FB0B, "0041bc90"),
        "maintenance": (0x40D960, 0x40DBB8, "0040d960"),
        "opoint": (0x41FC61, 0x4203B4, "0041bc90"),
        "dat-byte-loop": (0x414993, 0x4149C4, "004148a0"),
    }
    captures = {}
    for name, (lo, hi, owner) in ranges.items():
        data = pe.get_data(lo-base, hi-lo)
        instructions = list(dis.disasm(data, lo))
        assert sum(i.size for i in instructions) == len(data)
        exported = {int(i["address"],16): i for i in
                    json.loads((EXPORT/(owner+".json")).read_text())["assembly_bytes"]}
        for i in instructions:
            assert exported[i.address]["bytes"] == bytes(i.bytes).hex()
        (OUT/(name+".asm")).write_text("\n".join(
            f"{i.address:08x}  {bytes(i.bytes).hex():22} {i.mnemonic} {i.op_str}"
            for i in instructions)+"\n", encoding="utf-8")
        captures[name] = {"start":hex(lo), "end_exclusive":hex(hi),
                          "owner":owner, "instructions":len(instructions),
                          "sha256":hashlib.sha256(data).hexdigest()}

    u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    u.mem_map(base, (len(image)+4095)//4096*4096)
    u.mem_write(base, image)
    arena = 0x10000000
    u.mem_map(arena, 0x100000)
    game, src, dst, file, frame, stack = [arena+x for x in
                                       (0,0x2000,0x3000,0x4000,0x6000,0x10000)]
    def i32(a,v): u.mem_write(a,struct.pack("<I",v & 0xffffffff))
    def f64(a,v): u.mem_write(a,struct.pack("<d",v))
    def read_i32(a): return struct.unpack("<i",u.mem_read(a,4))[0]
    def read_f64(a): return struct.unpack("<d",u.mem_read(a,8))[0]
    stops=set()
    def stop_at_boundary(uc,address,size,user):
        if address in stops: uc.emu_stop()
    u.hook_add(unicorn.UC_HOOK_CODE,stop_at_boundary)
    def run(start,*boundaries):
        stops.clear(); stops.update(boundaries)
        u.emu_start(start,0,count=50000)
        pc=u.reg_read(unicorn.x86_const.UC_X86_REG_EIP)
        assert pc in stops, f"instruction budget exhausted at {pc:08x}"
    i32(game+0x194,src); i32(game+0x198,dst)
    i32(src+0x368,file)
    u.reg_write(UC_X86_REG_EBX,game)
    u.reg_write(UC_X86_REG_EDI,0)
    u.reg_write(UC_X86_REG_ESP,stack)
    count_mp=0
    with (OUT/"mp-oracle.csv").open("w",newline="",encoding="utf-8") as stream:
        w=csv.writer(stream)
        w.writerow(["hp","mp","id","type","status","parent","disabled","result"])
        # Every HP step in the normal range; plus exhaustive guard combinations.
        cases = itertools.chain(
            ((hp,499,oid,0,0,-1,0) for hp in range(0,601) for oid in (0,51,52)),
            itertools.product((-101,-1,0,1,100,299,300,399,400,499,500,501,1000),
                              (-1,0,149,150,499,500,505), (0,50,51,52,53),
                              (0,3), (-1,0,1), (-1,0), (0,1)))
        for hp,mp,oid,typ,status,parent,disabled in cases:
            for a,v in ((src+0x2fc,hp),(src+0x308,mp),(src+0x8,status),
                        (src+0x2f4,parent),(file+0x6f4,oid),(file+0x6f8,typ),
                        (0x450BD4,disabled)):
                i32(a,v)
            # At 41f994 EBX/EDI select object. Stop before HP regeneration and
            # resume at the MP-only block with the SAME object and type guard.
            u.reg_write(UC_X86_REG_EBX,game)
            u.reg_write(UC_X86_REG_EDI,0)
            u.reg_write(UC_X86_REG_ESP,stack)
            run(0x41F994,0x41F9AE,0x41FAF8)
            if u.reg_read(unicorn.x86_const.UC_X86_REG_EIP) != 0x41FAF8:
                run(0x41FA74,0x41FAF8)
            w.writerow([hp,mp,oid,typ,status,parent,disabled,read_i32(src+0x308)])
            count_mp+=1

    # A tiny FPU initializer supplies the live [0,1] stack at 420067, produced
    # by FLD1 at 41ff36 and FLDZ at 41ff73. The game arithmetic stays unmodified.
    bootstrap=arena+0x18000
    u.mem_write(bootstrap,bytes.fromhex("dbe3d9e8d9ee"))
    count_fan=0
    with (OUT/"opoint-oracle.csv").open("w",newline="",encoding="utf-8") as stream:
        w=csv.writer(stream)
        w.writerow(["facing","parent_right","dvx","index","count","right","vx","vz"])
        for facing,parent_right,dvx in itertools.product(
                (-50,-11,-1,0,1,2,9,10,11,12,20,21,22,30,40,50,51,52,100),
                (0,1),(-12,0,1,4,12)):
            i32(frame+0x818,facing); i32(frame+0x80c,dvx)
            i32(stack+0x38,frame)
            u.reg_write(UC_X86_REG_ESP,stack)
            u.reg_write(UC_X86_REG_ECX,frame); u.reg_write(UC_X86_REG_EDX,0)
            run(0x41FCB5,0x41FCE1)
            count=read_i32(stack+0x58); direction=read_i32(stack+0x54)
            assert count>=1
            for index in range(count):
                u.reg_write(UC_X86_REG_EAX,direction)
                u.reg_write(UC_X86_REG_EBX,game)
                u.reg_write(UC_X86_REG_EDI,0);u.reg_write(UC_X86_REG_ESI,1)
                i32(stack+0x4c,index); i32(stack+0x58,count)
                u.mem_write(src+0x80,bytes([0 if parent_right else 1]))
                f64(dst+0x50,0)
                run(bootstrap,bootstrap+6)
                run(0x420067,0x42017B)
                w.writerow([facing,parent_right,dvx,index,count,
                            int(u.mem_read(dst+0x80,1)[0]==0),
                            read_f64(dst+0x40),read_f64(dst+0x50)])
                count_fan+=1
    count_rest=0
    with (OUT/"opoint-rest-oracle.csv").open("w",newline="",encoding="utf-8") as stream:
        w=csv.writer(stream); w.writerow(["count","index","arest"])
        for count in (1,2,3,4,5,6,10,48):
            u.reg_write(UC_X86_REG_EBX,game); u.reg_write(UC_X86_REG_ESP,stack)
            i32(stack+0x40,count)
            for index in range(count):
                obj=arena+0x20000+index*0x800
                i32(game+0x194+index*4,obj); i32(stack+0x74+index*4,index)
                u.mem_write(obj,b"\0"*0x800)
            run(0x4202E6,0x4203AF,0x420E93)
            for index in range(count):
                obj=arena+0x20000+index*0x800
                w.writerow([count,index,read_i32(obj+0xec)])
                # Also record that the original seeds sibling-pair rest to 40;
                # the current port has no object/object rest matrix yet.
                for other in range(count):
                    if other!=index: assert u.mem_read(obj+0xf0+other,1)==b"\x28"
                count_rest+=1
    strings=json.loads((EXPORT/"_strings.json").read_text())
    key_text="SiuHungIsAGoodBearBecauseHeIsVeryGood"
    key_address=next(int(a,16) for a,s in strings.items() if s["value"]==key_text)
    key=pe.get_data(key_address-base,len(key_text))
    assert key==key_text.encode("ascii") and len(key)==37
    count_dat=0
    with (OUT/"dat-oracle.csv").open("w",newline="",encoding="utf-8") as stream:
        w=csv.writer(stream); w.writerow(["position","encrypted","plain"])
        for position,encrypted in itertools.product(range(37),range(256)):
            u.reg_write(UC_X86_REG_ESP,stack)
            u.reg_write(UC_X86_REG_ESI,(123+position)%37)
            u.mem_write(stack+0x1c,key+b"\0")
            i32(stack+0x10,encrypted)
            run(0x414993,0x4149C4)
            w.writerow([position,encrypted,read_i32(stack+0x10)])
            count_dat+=1
    constants={}
    for va in (0x447A00,0x449048,0x449280,0x449278,0x447A48):
        data=pe.get_data(va-base,8)
        constants[f"{va:08x}"]={"bytes":data.hex(),"double":struct.unpack("<d",data)[0]}
    result={"exe_sha256":SHA,"captures":captures,"constants":constants,
            "mp_cases":count_mp,"opoint_cases":count_fan,"dat_cases":count_dat,
            "opoint_rest_cases":count_rest,
            "dat_key":{"address":hex(key_address),"length":37,"header_bytes":123},
            "unicorn":unicorn.__version__,
            "scope":"isolated instruction ranges with synthetic memory; not a live game trace"}
    result["oracle_sha256"]={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sorted(OUT.glob("*-oracle.csv"))}
    (OUT/"binary-evidence.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))


if __name__ == "__main__":
    main()
