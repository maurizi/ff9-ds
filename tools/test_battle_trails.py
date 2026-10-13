#!/usr/bin/env python3
"""Compare the circular trail writer's complete buffer with original MIPS."""
import importlib.util
from pathlib import Path
import random
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ff9_equiv', ROOT/'decomp/tools/equiv.py')
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)
address = 0x800CF5A4
asm = ROOT/'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_3/func_800CF5A4.s'
image = b''.join(bytes.fromhex(w) for w in re.findall(
    r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/', asm.read_text()))
rng = random.Random(address)
cases, expected = [], []
for trial in range(400):
    capacity = rng.randrange(32)
    data = bytearray(rng.randbytes(512))
    data[1] = rng.randrange(capacity + 2)
    data[2] = capacity if trial % 2 else rng.randrange(256)
    width = rng.getrandbits(32)
    position = rng.randbytes(8)
    cases.append(struct.pack('<3I', capacity, width, trial % 2) + data + position)
    mem = e.Mem(trial, [(address, image), (0x80180000, data), (0x80181000, position)])
    cpu = e.Cpu(mem, address, address+len(image), {}, {}, trial,
                [0x80180000, capacity, width, 0x80181000])
    cpu.run(address)
    assert not cpu.events, cpu.events
    expected.append(struct.pack('<I', cpu.r[2]) + bytes(
        mem.read(0x80180000+i, 1) for i in range(512)))
result = subprocess.run([ROOT/'build/tests/battle_trails_probe'],
                        input=b''.join(cases), capture_output=True)
assert result.returncode == 0, result.stderr.decode()
assert result.stdout == b''.join(expected), 'trail buffer differs from original MIPS'
print('Battle trail writer matches original MIPS in 400 cases (raw/native pointers)')
