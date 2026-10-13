#!/usr/bin/env python3
"""Compare strip packets, context mutations and GTE state with original MIPS.

Uses the same deterministic GTE callback on both CPUs; verifies CPU/packet
logic, not GTE hardware accuracy.
"""
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ff9_equiv', ROOT / 'decomp/tools/equiv.py')
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)


class Cpu(e.Cpu):
    def gte_cmd(self, op):
        g = self.g
        if op & 63 == 0x10:
            g[22] = g[6] ^ g[8]
        elif op & 63 == 0x3d:
            for i in range(9, 12):
                g[i] = min(32767, g[8] * g[i])
        elif op & 63 == 0x30:
            for i in range(3):
                g[12+i] = g[i*2]
                g[17+i] = g[i*2+1] & 65535
        else:
            assert op & 63 == 1
            g[12:15] = [g[13], g[14], g[0]]
            g[17:20] = [g[18], g[19], g[1] & 65535]


def main():
    rng = random.Random(0xd4a30)
    asm = ROOT / 'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_2'
    # Include the handwritten non-ABI color helper, which reads t1 and s1.
    image = b''.join(bytes.fromhex(w) for addr in (0x800D4A08, 0x800D4A30)
                     for w in re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/',
                                         (asm / f'func_{addr:08X}.s').read_text()))
    cases, expected = [], []
    for trial in range(200):
        data = bytearray(rng.randbytes(1024))
        stride, count, rows = rng.choice((1, 2)), rng.randint(1, 3), rng.randint(1, 4)
        values = [rng.choice((0x38000000, 0x3a000000)), rng.randrange(4097),
                  0x02180100, 0x02180120, 0x02180180, 0x02180200,
                  rng.choice((0, 100, 0xffffffff)), rows, 0x02180204, count, rng.randrange(4)*32]
        struct.pack_into('<11I', data, 0, *values)
        struct.pack_into('<2H', data, 0x100, trial % 2, rng.randrange(8192))
        out, ot = rng.randbytes(1024), rng.randbytes(16384)
        regs = [rng.getrandbits(32) for _ in range(64)]
        if trial % 5 == 0:
            regs[24] = 0
        cases.append(struct.pack('<I', stride) + data + out + ot + struct.pack('<64I', *regs))
        original = bytearray(data)
        for offset in (8, 12, 16, 20, 32):
            struct.pack_into('<I', original, offset, struct.unpack_from('<I', data, offset)[0] + 0x7e000000)
        mem = e.Mem(trial, [(0x800D4A08, image), (0x80180000, bytes(original)),
                           (0x80181000, out), (0x80188000, ot)])
        cpu = Cpu(mem, 0x800D4A08, 0x800D4A08+len(image), {}, {}, trial,
                  [stride, 0x80180000, 0x80181000, 0x80188000])
        cpu.g = regs.copy()
        cpu.run(0x800D4A30)
        assert not cpu.events and not cpu.calls, (trial, cpu.events, cpu.calls)
        # Context contains no pointers in this output slice. Normalize OT links
        # by comparing low 24 bits, identical for KSEG0 and host addresses.
        expected.append(bytes(mem.read(0x80180024+i, 1) for i in range(24)) +
                        bytes(mem.read(0x80181000+i, 1) for i in range(1024)) +
                        bytes(mem.read(0x80188000+i, 1) for i in range(16384)) +
                        struct.pack('<64I', *cpu.g))
    result = subprocess.run([ROOT / 'build/tests/battle_strips_probe'],
                            input=b''.join(cases), capture_output=True)
    assert result.returncode == 0, result.stderr.decode()
    size = len(expected[0])
    assert len(result.stdout) == size * len(cases)
    for i, want in enumerate(expected):
        got = result.stdout[i*size:(i+1)*size]
        assert got == want, (i, next(j for j, (a, b) in enumerate(zip(got, want)) if a != b))
    print(f'Battle strips match original MIPS in {len(cases)} cases (shared GTE model)')


if __name__ == '__main__':
    main()
