#!/usr/bin/env python3
"""Compare production GTE commands with a paused PCSX-Redux instance.

Uses synthetic instructions/register vectors, preserves the reference state,
ends paused, and writes snapshots only under build/validation. Requires the
user's running decomp emulator and `tools/docker.sh make test-gte` first.
"""
import importlib.util
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import time
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ff9_emu',ROOT/'decomp/tools/emu.py')
emu=importlib.util.module_from_spec(spec);spec.loader.exec_module(emu)
assert not emu.is_running(), 'pause the reference emulator first'
rng=random.Random(0x9ff)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--suite',choices=('op','game'),default='op')
options=parser.parse_args()
cases=[]
for sf in (0,1):
    for lm in (0,1):
        for trial in range(8):
            regs=[0]*64
            for index in (9,10,11,32,34,36): regs[index]=rng.randrange(65536)
            if trial==0:
                for index,value in zip((9,10,11,32,34,36),(7,11,13,2,3,5)): regs[index]=value
            cases.append((0x0C|(sf<<19)|(lm<<10),regs))
if options.suite=='game':
    cases=[]
    commands=(1,0x30,6,0x2d,0x2e,0x12,0xc,0x3d,0x3e,0x10,0x2a,0x11,0x29,0x28,0x1e,0x20,0x1b,0x3f,0x13,0x16,0x1c,0x14)
    def pair(a,b):return (a&65535)|((b&65535)<<16)
    for command in commands:
        for lm in (0,1):
            for trial in range(4):
                regs=[0]*64
                for i in range(3):
                    regs[i*2]=pair(rng.randint(-4096,4096),rng.randint(-4096,4096))
                    regs[i*2+1]=rng.randint(256,8192)
                    regs[9+i]=rng.randint(-32768,32767)&0xffffffff
                    regs[12+i]=pair(rng.randint(-300,300),rng.randint(-200,200))
                    regs[20+i]=rng.getrandbits(32)
                    regs[25+i]=rng.randint(-1000000,1000000)&0xffffffff
                regs[6]=rng.getrandbits(32);regs[8]=rng.randrange(4097)
                for i in range(16,20):regs[i]=rng.randrange(8192)
                for base in (32,40,48):
                    for i in range(5):regs[base+i]=pair(rng.randint(-4096,4096),rng.randint(-4096,4096))
                for i in (37,38,39):regs[i]=rng.randint(-4096,4096)&0xffffffff
                for i in (45,46,47,53,54,55):regs[i]=rng.randrange(256)<<4
                regs[56]=160<<16;regs[57]=112<<16;regs[58]=300
                regs[59]=0xffffef9e;regs[60]=0x1400000;regs[61]=0x155;regs[62]=0x100
                cases.append((command|0x80000|(lm<<10),regs))
name='ds_gte_probe_'+time.strftime('%Y%m%d_%H%M%S')
emu.http('/state/save?name='+name)
expected=[]
try:
    for op,regs in cases:
        emu.bridge('write',0x801fee00,struct.pack('<64I',*regs).hex())
        emu.bridge('write',0x801fed00,struct.pack('<2I',0x4a000000|op,0).hex())
        emu.lua("local ffi=require('ffi'); local r=PCSX.getRegisters(); ffi.copy(r.CP2D.r,PCSX.getMemPtr()+0x1fee00,256); r.pc=0x801fed00; return true")
        bp=emu.bridge('addBp',0x801fed04,'Exec',4,'break',1,'synthetic GTE probe')
        try:
            emu.run_frames(1)
            assert emu.bridge('regs')['pc']==0x801fed04
            expected.append(emu.lua('local r=PCSX.getRegisters(); local out={}; for i=0,31 do out[i+1]=tonumber(r.CP2D.r[i]); out[i+33]=tonumber(r.CP2C.r[i]); end; return out'))
        finally:
            emu.bridge('removeBp',bp)
            emu.lua('Bridge.pauseAt=nil; return true')
finally:
    emu.http('/state/load?name='+name)
    emu.run_frames(0)
result=subprocess.run([ROOT/'build/tests/gte_probe'],input=b''.join(struct.pack('<65I',op,*regs) for op,regs in cases),capture_output=True)
assert result.returncode==0,result.stderr.decode()
assert not result.stderr,result.stderr.decode()
actual=[list(struct.unpack_from('<64I',result.stdout,i*256)) for i in range(len(cases))]
(ROOT/f'build/validation/gte_psx_{options.suite}_vectors.json').write_text(json.dumps({'cases':cases,'psx':expected,'native':actual},indent=2))
for i,(got,want) in enumerate(zip(actual,expected)):
    # Redux keeps IR in 16-bit union fields; architectural mfc2 sign-extends.
    for j in (9,10,11): got[j] &= 65535; want[j] &= 65535
    assert got==want,(i,hex(cases[i][0]),[(j,hex(a),hex(b)) for j,(a,b) in enumerate(zip(got,want)) if a!=b])
print(f'Production GTE {options.suite} suite matches PCSX-Redux in {len(cases)} cases (MAC/IR/FLAG and register preservation)')
