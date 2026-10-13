#!/usr/bin/env python3
"""Original MIPS versus native ribbon emitter, using the production GTE.

Checks context, packets, OT and raw GTE state; not a PSX hardware GTE check.
"""
import ctypes
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('equiv',ROOT/'decomp/tools/equiv.py')
e=importlib.util.module_from_spec(spec);spec.loader.exec_module(e)
lib=ctypes.CDLL(str(ROOT/'build/tests/gte_oracle.so'))
for name in ('gte_mfc2','gte_cfc2'):
    getattr(lib,name).restype=ctypes.c_uint32
for name in ('gte_mtc2','gte_ctc2'):
    getattr(lib,name).argtypes=[ctypes.c_int,ctypes.c_uint32]
lib.gte_cmd.argtypes=[ctypes.c_uint32]


class Registers:
    def __getitem__(self,index):
        return lib.gte_mfc2(index) if index<32 else lib.gte_cfc2(index-32)
    def __setitem__(self,index,value):
        (lib.gte_mtc2 if index<32 else lib.gte_ctc2)(index if index<32 else index-32,value)


class Cpu(e.Cpu):
    def gte_cmd(self,word):lib.gte_cmd(word)


def main():
    rng=random.Random(0xd5c30)
    asm=ROOT/'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_2/func_800D5C30.s'
    image=b''.join(bytes.fromhex(w) for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',asm.read_text()))
    binary=(ROOT/'decomp/disks/us/ovl/0b.bin').read_bytes()
    table=binary[0xcfa00-0xa7000:0xcfa00-0xa7000+512]
    cases=[];expected=[]
    for trial in range(200):
        count=rng.randint(1,8);size=rng.randint(3,8)
        data=bytearray(rng.randbytes(512))
        values=[rng.choice((0,100,0xffffffff)),0x02180080,4096//count,
                rng.getrandbits(24)|rng.choice((0x3c000000,0x3e000000)),rng.getrandbits(24)]
        struct.pack_into('<5I',data,0,*values)
        struct.pack_into('<2H3I2H',data,20,rng.randrange(256),rng.randrange(256),
                         rng.getrandbits(16)<<16,rng.getrandbits(16)<<16,4096//count,
                         rng.randrange(256)<<8,rng.randrange(256)<<8)
        data[128:132]=bytes((size-1,rng.randrange(size),0,0))
        for i in range(size):struct.pack_into('<4h',data,132+8*i,rng.randrange(-200,201),rng.randrange(-150,151),rng.randrange(256,4096),rng.randrange(128,4096))
        regs=[0]*64
        # Identity rotation, perspective and screen origin, with translation.
        regs[32:37]=[4096,0,4096,0,4096]
        regs[37:40]=[rng.randrange(-100,101),rng.randrange(-100,101),rng.randrange(128,1024)]
        regs[56:59]=[160<<16,112<<16,256]
        regs=[v&0xffffffff for v in regs]
        out,ot=rng.randbytes(1024),rng.randbytes(16384)
        cases.append(struct.pack('<2I',0,count)+data+out+ot+struct.pack('<64I',*regs))
        original=bytearray(data);struct.pack_into('<I',original,4,0x80180080)
        mem=e.Mem(trial,[(0x800d5c30,image),(0x80180000,bytes(original)),
                         (0x80181000,out),(0x80188000,ot),(0x800cfa00,table)])
        cpu=Cpu(mem,0x800d5c30,0x800d5c30+len(image),{}, {},trial,
                [count,0x80180000,0x80181000,0x80188000])
        state=ctypes.create_string_buffer(struct.pack('<64I',*regs),256)
        lib.oracle_set(state);cpu.g=Registers();cpu.run(0x800d5c30)
        assert not cpu.events and not cpu.calls,(trial,cpu.events,cpu.calls)
        lib.oracle_get(state)
        result=bytearray(mem.read(0x80180000+i,1) for i in range(512))
        struct.pack_into('<I',result,4,0x02180080)
        expected.append(result+bytes(mem.read(0x80181000+i,1) for i in range(1024))+
                        bytes(mem.read(0x80188000+i,1) for i in range(16384))+state.raw)
    asmroot=asm.parent
    particle_image=b''.join(bytes.fromhex(w) for address in (0x800dff5c,0x800dff64)
                           for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',
                                               (asmroot/f'func_{address:08X}.s').read_text()))
    for kind in (1,2):
        for trial in range(100):
            data=rng.randbytes(512);out=rng.randbytes(1024);ot=rng.randbytes(16384)
            speed=rng.getrandbits(32);regs=[0]*64
            cases.append(struct.pack('<2I',kind,speed)+data+out+ot+struct.pack('<64I',*regs))
            mem=e.Mem(trial,[(0x800dff5c,particle_image),(0x80180000,data),(0x80181000,out),(0x80188000,ot)])
            cpu=Cpu(mem,0x800dff5c,0x800dff5c+len(particle_image),{}, {},trial,
                    [0x80180000,speed,0x80180080,0x80181000])
            state=ctypes.create_string_buffer(struct.pack('<64I',*regs),256)
            lib.oracle_set(state);cpu.g=Registers();cpu.run(0x800dff5c if kind==1 else 0x800dff64)
            assert not cpu.events and not cpu.calls
            lib.oracle_get(state)
            expected.append(bytes(mem.read(0x80180000+i,1) for i in range(512))+
                            bytes(mem.read(0x80181000+i,1) for i in range(1024))+
                            bytes(mem.read(0x80188000+i,1) for i in range(16384))+state.raw)
    result=subprocess.run([ROOT/'build/tests/battle_ribbons_probe'],input=table+b''.join(cases),capture_output=True)
    assert result.returncode==0,result.stderr.decode()
    size=len(expected[0]);assert len(result.stdout)==size*len(cases),(len(result.stdout),size*len(cases))
    for i,want in enumerate(expected):
        got=result.stdout[i*size:(i+1)*size]
        assert got==want,(i,next(j for j,(a,b) in enumerate(zip(got,want)) if a!=b))
    print(f'Battle ribbons match original MIPS in {len(cases)} cases with production GTE')


if __name__=='__main__':main()
