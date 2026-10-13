#!/usr/bin/env python3
"""Particle integration CPU equivalence with a shared GTE model."""
import importlib.util
from pathlib import Path
import random,re,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ff9_equiv',ROOT/'decomp/tools/equiv.py'); e=importlib.util.module_from_spec(spec); spec.loader.exec_module(e)
def short(v): return struct.unpack('<h',struct.pack('<H',v&65535))[0]
class Cpu(e.Cpu):
    def gte_cmd(self,op):
        assert op&63 in (0x3D,0x3E)
        shift=12 if op&(1<<19) else 0
        for i in range(3):
            value=short(self.g[8])*short(self.g[9+i])
            if op&63==0x3E: value+=e.s32(self.g[25+i])*(1<<shift)
            value>>=shift
            self.g[25+i]=value&0xffffffff
            self.g[9+i]=max(-32768,min(32767,value))&0xffffffff
rng=random.Random(0xdfe4c); cases=[]; expected=[]
base=0x800DFE4C
image=b''
for addr in (base,base+8):
    asm=ROOT/f'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_2/func_{addr:08X}.s'
    image+=b''.join(bytes.fromhex(w) for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',asm.read_text()))
for kind in range(2):
    for trial in range(200):
        data=rng.randbytes(64); gravity=rng.getrandbits(32); regs=[rng.getrandbits(32) for _ in range(64)]
        cases.append(struct.pack('<2I',kind,gravity)+data+struct.pack('<64I',*regs))
        mem=e.Mem(trial,[(base,image),(0x80180000,data)])
        cpu=Cpu(mem,base,base+len(image),{}, {},trial,[0x80180000,gravity,0x80180030,0x80180038]); cpu.g=regs.copy();cpu.run(base+kind*8)
        assert not cpu.events and not cpu.calls,(kind,trial,cpu.events,cpu.calls)
        expected.append(bytes(mem.read(0x80180000+i,1) for i in range(64))+struct.pack('<64I',*cpu.g))
result=subprocess.run([ROOT/'build/tests/battle_motion_probe'],input=b''.join(cases),capture_output=True);assert result.returncode==0,result.stderr.decode()
size=len(expected[0]);assert len(result.stdout)==len(cases)*size
for i,want in enumerate(expected):
    got=result.stdout[i*size:(i+1)*size]
    assert got==want,(i,next(j for j,(a,b) in enumerate(zip(got,want)) if a!=b))
print(f'Battle motion matches original CPU logic in {len(cases)} cases (shared GTE model)')
