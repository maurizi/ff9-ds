#!/usr/bin/env python3
"""Compare native battle scalar helpers with original MIPS instructions from the decomp.

The decomp's interpreter models the CPU used by these helpers (no mocked GTE/calls here).
Game-derived instructions/vectors are read/generated at runtime, never embedded in this test.
"""
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ff9_equiv', ROOT / 'decomp/tools/equiv.py')
equiv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(equiv)
addresses = [0x800CF58C, 0x800CF560, 0x800CF574, 0x800CF690, 0x800CF708,
             0x800CF748, 0x800CF788, 0x800CF6B8, 0x800CF9DC]
rng = random.Random(0xFF09)
rows, expected = [], []
for kind, address in enumerate(addresses):
    asm = ROOT / f'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_3/func_{address:08X}.s'
    words = re.findall(r'/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/', asm.read_text())
    image = b''.join(bytes.fromhex(value) for _, value in words)
    for trial in range(300):
        seed, a, b, c, d = [rng.getrandbits(32) for _ in range(5)]
        phase = rng.randrange(0x10000)
        if kind == 4 and trial < 10: b = a  # equal bounds do not advance RNG
        if kind == 5 and trial < 10: a = 0
        if kind == 7:
            a = rng.randrange(0x80000000) if trial >= 10 else trial
            b = rng.choice([1, 2, 3, 4, 15, 20])
        if kind == 8 and trial < 10: d = 0  # raw MIPS DIV semantics
        rows.append(f'{kind} {seed:x} {a:x} {b:x} {c:x} {d:x} {phase:x}')
        mem = equiv.Mem(trial, [(address, image)])
        mem.write(0x1F8003FC, seed, 4)
        mem.write(0x800CF588, c, 4)
        mem.write(0x800CF5A0, d, 4)
        mem.write(0x800FE528, phase, 2)
        mem.write(0x801FC000, 0, 8)
        args = [0x801FC000 if kind == 6 else a, b, c, d]
        cpu = equiv.Cpu(mem, address, address + len(image), {}, {}, trial, args)
        cpu.run(address)
        assert not cpu.events and not cpu.calls, (kind, trial, cpu.events, cpu.calls)
        expected.append([0 if kind in (0, 1, 6) else cpu.r[2], mem.read(0x1F8003FC, 4),
                         mem.read(0x800CF588, 4), mem.read(0x800CF5A0, 4),
                         mem.read(0x800FE528, 2), *[mem.read(0x801FC000 + i, 2) for i in (0, 2, 4)]])
result = subprocess.run([ROOT / 'build/tests/battle_math_probe'], input='\n'.join(rows) + '\n',
                        text=True, capture_output=True, check=True)
actual = [[int(v, 16) for v in line.split()] for line in result.stdout.splitlines()]
assert len(actual) == len(expected), result.stderr
for index, (got, want) in enumerate(zip(actual, expected)):
    assert got == want, (rows[index], got, want)
print(f'Battle math matches original MIPS in {len(rows)} cases (returns, RNG and state writes)')
