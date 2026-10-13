#!/usr/bin/env python3
"""Battle pose CPU equivalence against original MIPS, using a shared simple GTE
and rotation callback. Checks hierarchy, packed tracks, scratch/main-RAM paths;
this does not independently test the existing rotation function or GTE core."""
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ff9_equiv', ROOT / 'decomp/tools/equiv.py')
e = importlib.util.module_from_spec(spec); spec.loader.exec_module(e)
def short(v): return struct.unpack('<h',struct.pack('<H',v&65535))[0]
class Cpu(e.Cpu):
    def external(self, target):
        assert target == 0x80031264, hex(target)
        r = [short(self.m.read(self.r[4]+i*2,2)) for i in range(3)]
        for i in range(9): self.m.write(self.r[5]+i*2, (4096 if i%4==0 else 0)+r[i%3],2)
    def gte_cmd(self, op):
        assert op & 63 == 0x12
        g = self.g
        matrix = [short(g[32+i//2] >> ((i&1)*16)) for i in range(9)]
        vector = [short(g[9+i]) if op>>15&3==3 else short(g[0]>>(i*16) if i<2 else g[1]) for i in range(3)]
        for row in range(3):
            value = e.s32(g[37+row])*4096 if op>>13&3==0 else 0
            value += sum(matrix[row*3+c]*vector[c] for c in range(3))
            value >>= 12
            g[25+row] = value & 0xffffffff; g[9+row] = max(-32768,min(32767,value)) & 0xffffffff
asm = ROOT/'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_4/func_800C7DAC.s'
image = b''.join(bytes.fromhex(w) for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',asm.read_text()))
rng = random.Random(0xc7dac)
cases, expected = [], []
for trial in range(100):
    blob = bytearray(8192); count = (2,6,30,31,36)[trial%5]; frame = trial%7
    animated = trial%10 >= 2; parent = bool(trial%2); low = bool(trial%3)
    pointers = {8:256, 256+12:4096}
    if animated: pointers.update({12:512, 512+12:1024})
    if animated and low: pointers[512+16] = 1536
    if parent: pointers.update({28:2048, 2048+32:2304})
    for off,value in pointers.items(): struct.pack_into('<I',blob,off,0x02180000+value)
    blob[256+2] = count; blob[4] = 2
    for i in range(9): struct.pack_into('<h',blob,36+i*2,(4096 if i%4==0 else 0)+rng.randrange(-100,101))
    for i in range(3): struct.pack_into('<i',blob,56+i*4,rng.randrange(-500,501))
    struct.pack_into('<hhhH',blob,116,*(rng.randrange(-100,101) for _ in range(3)),0)
    for b in range(4):
        for i in range(9): struct.pack_into('<h',blob,2304+b*32+i*2,(4096 if i%4==0 else 0)+rng.randrange(-100,101))
        for i in range(3): struct.pack_into('<i',blob,2304+b*32+20+i*4,rng.randrange(-500,501))
    flags = trial%8; struct.pack_into('<H',blob,512+10,flags)
    for i in range(3):
        field = rng.randrange(100) if flags & (1<<i) else 3000+i*32
        struct.pack_into('<H',blob,512+4+i*2,field)
        if not flags & (1<<i):
            for j in range(8): struct.pack_into('<h',blob,512+field+j*2,rng.randrange(-500,501))
    blob[512+3500:512+3756] = rng.randbytes(256)
    for bone in range(count):
        for offset in (1024,1536):
            keyflags = rng.randrange(8); fields = [rng.randrange(256) if keyflags&(1<<i) else 3500+i*32 for i in range(3)]
            struct.pack_into('<4H',blob,offset+bone*8,*fields,keyflags)
        struct.pack_into('<hBB',blob,4096+bone*4,rng.randrange(-200,201),0,rng.randrange(max(1,bone)))
    cases.append(struct.pack('<I',frame)+blob)
    original = bytearray(blob)
    for off,value in pointers.items(): struct.pack_into('<I',original,off,0x80180000+value)
    mem=e.Mem(trial,[(0x800C7DAC,image),(0x80180000,original),(0x80190000,bytes(4096)),(0x1f800000,bytes(1024))])
    cpu=Cpu(mem,0x800C7DAC,0x800C86C8,{}, {},trial,[0x80180000,frame,0x80190000,0]); cpu.g=[0]*64
    cpu.run(0x800C7DAC); assert not cpu.events, (trial,cpu.events)
    output=struct.pack('<2I64I',(cpu.r[2]-0x80190000)&0xffffffff,mem.read(0x80180020,4)&0xffffff,*cpu.g)
    for base,size in ((0x1f800000,1024),(0x80190000,4096)): output += bytes(mem.read(base+i,1) for i in range(size))
    expected.append(output)
result=subprocess.run([ROOT/'build/tests/battle_pose_probe'],input=b''.join(cases),capture_output=True)
assert result.returncode == 0,result.stderr.decode()
actual=result.stdout; size=len(expected[0]); assert len(actual)==size*len(cases)
for trial,want in enumerate(expected):
    got=actual[trial*size:(trial+1)*size]
    if got!=want:
        at=next(i for i,(a,b) in enumerate(zip(got,want)) if a!=b)
        raise AssertionError((trial,at,got[at:at+16].hex(),want[at:at+16].hex()))
print(f'Battle pose matches original CPU logic in {len(cases)} cases (shared GTE/rotation model)')
