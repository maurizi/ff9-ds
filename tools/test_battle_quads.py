#!/usr/bin/env python3
"""Projected effect rectangles: CPU/packet equivalence with shared GTE."""
import importlib.util
from pathlib import Path
import random,re,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ff9_equiv',ROOT/'decomp/tools/equiv.py');e=importlib.util.module_from_spec(spec);spec.loader.exec_module(e)
class Cpu(e.Cpu):
    def gte_cmd(self,op):
        g=self.g
        if op&63==0x10: g[22]=g[6]^g[8]
        elif op&63==0x30:
            for i in range(3): g[12+i]=g[i*2];g[17+i]=g[i*2+1]&65535
        else:
            assert op&63==1
            g[12:15]=[g[13],g[14],g[0]];g[17:20]=[g[18],g[19],g[1]&65535]
rng=random.Random(0xd4478);cases=[];expected=[]
for kind,address in enumerate((0x800D4478,0x800D40CC,0x800D45FC,0x800D3ED4,0x800D4228,0x800D437C)):
    asm=ROOT/f'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_2/func_{address:08X}.s'
    image=b''.join(bytes.fromhex(w) for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',asm.read_text()))
    for trial in range(100):
        data=bytearray(rng.randbytes(288));struct.pack_into('<I',data,244,trial%7)
        struct.pack_into('<I',data,248,rng.choice([0,0xffffffff,0xfffff000,0x1000]))
        if kind in (1,3,4,5):
            struct.pack_into('<2I',data,224,0x02000000+0x180064,0x02000000+0x180084)
            struct.pack_into('<I',data,252,1+trial%6)
            if kind==3:struct.pack_into('<2I',data,232,1+trial%32,1+trial%24)
        elif kind==2:
            struct.pack_into('<I',data,248,0x02181080)
            for j in range(6): struct.pack_into('<H',data,j*10+8,rng.choice((0,0,1,2,2,7)))
        out=rng.randbytes(256);ot=rng.randbytes(16384);regs=[rng.getrandbits(32) for _ in range(64)]
        if trial%5==0: regs[24]=0
        cases.append(struct.pack('<I',kind)+data+out+ot+struct.pack('<64I',*regs))
        # Original pointers use KSEG0; native pointers use the fixed host arena.
        original=bytearray(data)
        if kind in (1,3,4,5): struct.pack_into('<2I',original,224,0x80180064,0x80180084)
        elif kind==2: struct.pack_into('<I',original,248,0x80181080)
        mem=e.Mem(trial,[(address,image),(0x80180000,bytes(original)),(0x80181000,out),(0x80188000,ot)])
        cpu=Cpu(mem,address,address+len(image),{}, {},trial,[0x80180000,0x801800e0,0x80181000,0x80188000]);cpu.g=regs.copy();cpu.run(address)
        assert not cpu.events and not cpu.calls,(trial,cpu.events,cpu.calls)
        if kind==2: mem.write(0x801810fc,cpu.r[2]&0xffffff,4)
        expected.append(bytes(mem.read(0x80181000+i,1) for i in range(256))+bytes(mem.read(0x80188000+i,1) for i in range(16384))+struct.pack('<64I',*cpu.g))
result=subprocess.run([ROOT/'build/tests/battle_quads_probe'],input=b''.join(cases),capture_output=True);assert result.returncode==0,result.stderr.decode()
size=len(expected[0]);assert len(result.stdout)==len(cases)*size
for i,want in enumerate(expected):
    got=result.stdout[i*size:(i+1)*size];assert got==want,(i,next(j for j,(a,b) in enumerate(zip(got,want)) if a!=b))
print(f'Battle effect quads match original CPU logic in {len(cases)} cases (shared GTE model)')
