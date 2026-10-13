#!/usr/bin/env python3
"""Orientation-helper CPU equivalence; shared simple GTE and atan callback.
Original atan and GTE accuracy are not independently validated by this test."""
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ff9_equiv',ROOT/'decomp/tools/equiv.py'); e=importlib.util.module_from_spec(spec); spec.loader.exec_module(e)
def short(v): return struct.unpack('<h',struct.pack('<H',v&65535))[0]
class Cpu(e.Cpu):
    def external(self,target):
        assert target==0x80053204,hex(target)
        self.r[2]=(self.r[4]*3+self.r[5]*5)&0xffffffff
    def gte_cmd(self,op):
        g=self.g
        if op&63==0x28:
            for i in range(9,12):
                square=short(g[i])**2; g[16+i]=square; g[i]=min(square,32767)
            return
        if op&63==0x11:
            for i in range(3):
                source=short(g[9+i]); delta=max(-32768,min(32767,e.s32(g[53+i])-source))
                value=(source*4096+delta*short(g[8]))>>12
                g[25+i]=value&0xffffffff; g[9+i]=max(-32768,min(32767,value))&0xffffffff
            return
        assert op&63==0x12
        matrix=[short(g[32+i//2]>>((i&1)*16)) for i in range(9)]
        vector=[short(g[9+i]) if op>>15&3==3 else short(g[0]>>(i*16) if i<2 else g[1]) for i in range(3)]
        for row in range(3):
            value=e.s32(g[37+row])*4096 if op>>13&3==0 else 0
            value+=sum(matrix[row*3+c]*vector[c] for c in range(3)); value>>=12
            g[25+row]=value&0xffffffff; g[9+row]=max(-32768,min(32767,value))&0xffffffff
rng=random.Random(0xcf808); cases=[]; expected=[]
for kind,address in enumerate((0x800CF808,0x800CF91C,0x800CF8EC,0x800CFA00,0x800CF9B0)):
    asm=ROOT/f'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_3/func_{address:08X}.s'
    image=b''.join(bytes.fromhex(w) for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',asm.read_text()))
    for trial in range(200):
        data=rng.randbytes(16); registers=[rng.getrandbits(32) for _ in range(64)]
        for i in range(5): registers[32+i]=0
        matrix=[(4096 if i%4==0 else 0)+rng.randrange(-100,101) for i in range(9)]
        for i,v in enumerate(matrix): registers[32+i//2]|=(v&65535)<<((i&1)*16)
        for i in range(3): registers[37+i]=rng.randrange(-200,201)&0xffffffff
        cases.append(struct.pack('<I',kind)+data+struct.pack('<64I',*registers))
        mem=e.Mem(trial,[(address,image),(0x80180000,data+bytes(8))])
        args=[0x80180000,0x80180008,0x80180010,0] if kind==0 else [0x80180000,0x80180010,0x80180014,0] if kind==1 else [int.from_bytes(data[:4],'little'),int.from_bytes(data[4:8],'little'),0x80180010,0]
        if kind==3: args=[0x80180000,0x80180008,int.from_bytes(data[12:16],'little'),0x80180010]
        if kind==4: args=[int.from_bytes(data[i:i+4],'little') for i in (0,4,8)]+[0]
        cpu=Cpu(mem,address,address+len(image),{}, {},trial,args); cpu.g=registers.copy(); cpu.run(address)
        assert not cpu.events,(kind,trial,cpu.events)
        expected.append(struct.pack('<I',cpu.r[2] if kind in (1,4) else 0)+bytes(mem.read(0x80180010+i,1) for i in range(8))+struct.pack('<64I',*cpu.g))
result=subprocess.run([ROOT/'build/tests/battle_angles_probe'],input=b''.join(cases),capture_output=True); assert result.returncode==0,result.stderr.decode()
actual=result.stdout; size=len(expected[0]); assert len(actual)==size*len(cases)
for trial,want in enumerate(expected):
    got=actual[trial*size:(trial+1)*size]
    if got!=want:
        at=next(i for i,(a,b) in enumerate(zip(got,want)) if a!=b)
        raise AssertionError((result.stderr.decode(),trial,at,got[at:at+16].hex(),want[at:at+16].hex()))
print(f'Battle angles match original CPU logic in {len(cases)} cases (shared GTE/atan model)')
