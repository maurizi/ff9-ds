#!/usr/bin/env python3
"""Differential integer/delay-slot/GTE glue tests against decomp's MIPS CPU.

The C probe additionally verifies the native 16-word o32 argument bridge.
Uses a shared deterministic GTE model, not a GTE accuracy test.
"""
import importlib.util
from pathlib import Path
import random
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('equiv',ROOT/'decomp/tools/equiv.py')
e=importlib.util.module_from_spec(spec);spec.loader.exec_module(e)


class Cpu(e.Cpu):
    def gte_cmd(self,op):
        assert op&63==0x10
        self.g[22]=self.g[6]^self.g[8]


def main():
    rng=random.Random(0xeffec7)
    cases=[];expected=[]
    # Shift/ALU, HI/LO, signed/unsigned division, immediate ALU, delay slots,
    # zero register, signed memory loads, scratchpad and COP2 transfers.
    for trial in range(300):
        words=[]
        for index,(fun,shift) in enumerate(((0,7),(2,13),(3,19),(4,0),(6,0),(7,0),
                                         (33,0),(35,0),(36,0),(37,0),(38,0),(39,0),(42,0),(43,0))):
            words += [(4<<21)|(5<<16)|(8<<11)|(shift<<6)|fun,
                      (43<<26)|(6<<21)|(8<<16)|(index*4)]
        # Return quotient XOR remainder for an independent observable result.
        words += [(4<<21)|(5<<16)|rng.choice((24,25,26,27)),
                  (8<<11)|16,(9<<11)|18,(8<<21)|(9<<16)|(2<<11)|38]
        # Branches must execute the slot and skip the following instruction.
        words += [0x10000002,0x24420001,0x24420100]
        # COP2 register transfers followed by DPCS and a data-register read.
        words += [(18<<26)|(4<<21)|(4<<16)|(6<<11),
                  (18<<26)|(4<<21)|(5<<16)|(8<<11),0x4a780010,
                  (18<<26)|(8<<16)|(22<<11),0,
                  (43<<26)|(6<<21)|(8<<16)|64,
                  0x03e00008,0x24420002]
        code=struct.pack('<%dI'%len(words),*words)
        a,b=rng.getrandbits(32),rng.getrandbits(32)
        if trial%10==0:b=0
        if trial%10==1:a,b=0x80000000,0xffffffff
        data=rng.randbytes(128);regs=[rng.getrandbits(32) for _ in range(64)]
        args=[a,b,0x80180000,rng.getrandbits(32)]
        cases.append(struct.pack('<6I',len(code),*args,0)+code+data+struct.pack('<64I',*regs))
        mem=e.Mem(trial,[(0x801e7700,code),(0x80180000,data)])
        cpu=Cpu(mem,0x801e7700,0x801e7700+len(code),{}, {},trial,args)
        cpu.g=regs.copy();cpu.run(0x801e7700)
        assert not cpu.events and not cpu.calls,(trial,cpu.events,cpu.calls)
        expected.append(struct.pack('<I',cpu.r[2])+bytes(mem.read(0x80180000+i,1) for i in range(128))+struct.pack('<64I',*cpu.g))
    # Optional original disc-derived module fixture. Modes 0/1 must preserve
    # original data initialization before testing its exported render calls.
    module=ROOT/'build/validation/ark_dynamic_effect_module.bin'
    if module.exists():
        code=bytearray(module.read_bytes())
        struct.pack_into('<2I',code,0,0x08000000|((0x801e8178>>2)&0x3ffffff),0)
        code=bytes(code)
        for mode in (0,1):
            for trial in range(30):
                data=bytearray(rng.randbytes(128))
                struct.pack_into('<I',data,12,0x80180020)
                args=[mode,0x80180060,0x80180000,0]
                fifth=0x80180060
                regs=[rng.getrandbits(32) for _ in range(64)]
                cases.append(struct.pack('<6I',len(code),*args,fifth)+code+data+struct.pack('<64I',*regs))
                mem=e.Mem(trial,[(0x801e7700,code),(0x80180000,bytes(data))])
                mem.write(e.STACK_TOP+16,fifth,4)
                cpu=Cpu(mem,0x801e7700,0x801e7700+len(code),{}, {},trial,args)
                cpu.g=regs.copy();cpu.run(0x801e7700)
                assert not cpu.events and not cpu.calls,(mode,trial,cpu.events,cpu.calls)
                expected.append(struct.pack('<I',cpu.r[2])+bytes(mem.read(0x80180000+i,1) for i in range(128))+struct.pack('<64I',*cpu.g))
    result=subprocess.run([ROOT/'build/tests/effect_vm_probe'],input=b''.join(cases),capture_output=True)
    assert result.returncode==0,result.stderr.decode()
    size=len(expected[0]);assert len(result.stdout)==size*len(cases)
    for i,want in enumerate(expected):
        got=result.stdout[i*size:(i+1)*size]
        assert got==want,(i,next(j for j,(a,b) in enumerate(zip(got,want)) if a!=b))
    print(f'Effect VM matches independent MIPS CPU in {len(cases)} cases; native 16-word bridge passes')


if __name__=='__main__':main()
