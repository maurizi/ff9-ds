#!/usr/bin/env python3
"""Battle packet CPU equivalence, all eight primitive formats; shared simple
NCLIP/AVSZ3 model. Real GTE precision and rasterisation are separate checks."""
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ff9_equiv', ROOT/'decomp/tools/equiv.py'); e=importlib.util.module_from_spec(spec); spec.loader.exec_module(e)
class Cpu(e.Cpu):
    def gte_cmd(self,op):
        g=self.g
        if op&63==6:
            x=[struct.unpack('<h',struct.pack('<H',g[12+i]&65535))[0] for i in range(3)]
            y=[struct.unpack('<h',struct.pack('<H',g[12+i]>>16))[0] for i in range(3)]
            g[24]=sum(x[i]*(y[(i+1)%3]-y[(i+2)%3]) for i in range(3))&0xffffffff
        elif op&63==0x2d: g[7]=(((g[17]+g[18]+g[19])&0xffffffff)>>4)&65535
        else: raise AssertionError(hex(op))
formats=[(24,4,21,8,16,19),(20,3,18,12,8,6),(32,4,28,8,16,29),(24,3,15,6,16,22),(24,4,20,0,8,0),(20,3,19,0,8,0),(16,4,13,0,8,0),(12,3,11,0,8,0)]
asm=ROOT/'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_4/func_800C58E4.s'
image=b''.join(bytes.fromhex(w) for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',asm.read_text()))
rng=random.Random(0xc58e4); cases=[]; expected=[]
for trial in range(160):
    blob=bytearray(8192); mesh=trial%2; base=384+mesh*40
    ptrs={8:256,24:3584,256+16:384,base+28:1024,base+32:2560,base+36:3072,3968+28:3840}
    if trial%3==0: ptrs[16]=0x10100
    for off,value in ptrs.items(): struct.pack_into('<I',blob,off,0x02180000+value)
    struct.pack_into('<H',blob,0,0x10 if trial%13==0 else 0)
    blob[3840+8]=trial%2
    struct.pack_into('<2I',blob,3840+16,0x02188100,0x02189100)
    for i in range(32):
        struct.pack_into('<hh',blob,512+i*4,rng.randrange(-200,201),rng.randrange(-200,201))
        struct.pack_into('<I',blob,640+i*4,rng.randrange(4096))
        struct.pack_into('<H',blob,2560+i*2,rng.getrandbits(16))
        struct.pack_into('<I',blob,3072+i*4,rng.getrandbits(32))
        struct.pack_into('<I',blob,3584+i*4,rng.getrandbits(32))
    blob[base+19]=rng.randrange(-2,3)&255
    at=1024
    for t,(stride,n,flags,uv,col,tex) in enumerate(formats):
        count=rng.randrange(4); struct.pack_into('<H',blob,base+2+t*2,count)
        for p in range(count):
            blob[at:at+stride]=rng.randbytes(stride)
            for i in range(n):
                struct.pack_into('<H',blob,at+i*2,rng.randrange(32))
                if uv: struct.pack_into('<H',blob,at+uv+i*2,rng.randrange(32))
                if 2<=t<6: struct.pack_into('<H',blob,at+col+i*2,rng.randrange(32))
            blob[at+flags]=rng.randrange(256)
            if t<4: blob[at+tex]=rng.randrange(32)
            at+=stride
    cases.append(struct.pack('<I',mesh)+blob)
    original=bytearray(blob)
    for off,value in ptrs.items(): struct.pack_into('<I',original,off,0x80180000+value)
    struct.pack_into('<2I',original,3840+16,0x80188100,0x80189100)
    mem=e.Mem(trial,[(0x800C58E4,image),(0x80180000,original),(0x80188000,bytes(16384)),(0x80190000,bytes(4096)),(0x800711A8,blob[512:640]),(0x80070520,blob[640:768])])
    mem.write(0x8006794C,0x80180F80,4)
    cpu=Cpu(mem,0x800C58E4,0x800C6528,{}, {},trial,[0x80180000,mesh,0x80190000,0]); cpu.g=[0]*64
    cpu.run(0x800C58E4); assert not cpu.events and not cpu.calls,(trial,cpu.events,cpu.calls)
    output=struct.pack('<2I64I',(cpu.r[2]-0x80190000)&0xffffffff,mem.read(0x80180010,4)&0xffffff,*cpu.g)
    for base,size in ((0x80188000,16384),(0x80190000,4096)): output+=bytes(mem.read(base+i,1) for i in range(size))
    expected.append(output)
result=subprocess.run([ROOT/'build/tests/battle_packets_probe'],input=b''.join(cases),capture_output=True)
assert result.returncode==0,result.stderr.decode()
actual=result.stdout; size=len(expected[0]); assert len(actual)==size*len(cases)
for trial,want in enumerate(expected):
    got=actual[trial*size:(trial+1)*size]
    if got!=want:
        at=next(i for i,(a,b) in enumerate(zip(got,want)) if a!=b)
        raise AssertionError((trial,at,got[at:at+16].hex(),want[at:at+16].hex()))
print(f'Battle packets match original CPU logic in {len(cases)} cases (eight formats, shared GTE model)')
