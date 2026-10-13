#!/usr/bin/env python3
"""Compare battle background CPU logic with original instructions using a shared
simple GTE model. This checks packing, scratch layout, culling and OT insertion;
it does not independently validate GTE arithmetic or DS rasterisation."""
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
        if op & 63 == 0x30:
            for i in range(3): g[12+i], g[17+i] = g[i*2], g[i*2+1] & 65535
        elif op & 63 == 6:
            x = [struct.unpack('<h', struct.pack('<H', g[12+i] & 65535))[0] for i in range(3)]
            y = [struct.unpack('<h', struct.pack('<H', g[12+i] >> 16))[0] for i in range(3)]
            g[24] = sum(x[i]*(y[(i+1)%3]-y[(i+2)%3]) for i in range(3)) & 0xffffffff
        elif op & 63 == 0x3d:
            for i in range(9,12): g[i] = min(g[8]*g[i], 32767)
        else: raise AssertionError(hex(op))
images = []
for address in (0x800CEED4, 0x800CF448):
    path = ROOT / f'decomp/asm/us/ovl_0b/nonmatchings/ovl_0b_3/func_{address:08X}.s'
    words = re.findall(r'/\* [0-9A-F]+ [0-9A-F]{8} ([0-9A-F]{8}) \*/', path.read_text())
    images.append((address, b''.join(bytes.fromhex(w) for w in words)))
rng = random.Random(9)
cases, expected = [], []
for trial in range(160):
    rec, grp = bytearray(512), bytearray(512)
    flags = (0,1,2,3,8,9,10,0xffff)[trial % 8]
    count = (6,12,18,24)[trial % 4]
    struct.pack_into('<8H', rec, 0, flags, count, 0, 32, 96, 192, 5, 5)
    struct.pack_into('<HH', grp, 10, 16, 0)
    struct.pack_into('<H', grp, 14, 144)
    for i in range(32): struct.pack_into('<I', grp, 16+i*4, rng.getrandbits(32))
    for i in range(count): struct.pack_into('<hhH', grp, 144+i*6, rng.randrange(-200,201), rng.randrange(-200,201), rng.randrange(65536))
    for p in range(10):
        struct.pack_into('<I', rec, 32+p*4, rng.getrandbits(32))
        n, base = (4, 96+p*8) if p < 5 else (3, 136+(p-5)*6)
        for i in range(n): struct.pack_into('<H', rec, base+i*2, rng.randrange(count)*4)
    rec[192:262] = rng.randbytes(70)
    brightness, shift = (0,1,127,128,129,255,1024,65535)[trial % 8], trial % 3
    settings = struct.pack('<II', brightness, shift)
    cases.append(settings + rec + grp)
    mem = e.Mem(trial, images + [(0x80180000, rec), (0x80181000, grp), (0x80188000, bytes(16384)), (0x80190000, bytes(4096)), (0x1f800000, bytes(1024))])
    mem.write(0x800CF588, brightness, 4); mem.write(0x800CF5A0, shift, 4)
    cpu = Cpu(mem, 0x800CEED4, 0x800CF560, {}, {}, trial, [0x80180000, 0x80190000, 0x80188000, 0x80181000])
    cpu.g = [0]*64
    cpu.run(0x800CEED4)
    assert not cpu.calls and not cpu.events, (trial,cpu.calls,cpu.events)
    output = struct.pack('<I64I', (cpu.r[2]-0x80190000) & 0xffffffff, *cpu.g)
    for base, size in ((0x1f800000,1024),(0x80188000,16384),(0x80190000,4096)):
        output += bytes(mem.read(base+i,1) for i in range(size))
    expected.append(output)
result = subprocess.run([ROOT/'build/tests/battle_background_probe'], input=b''.join(cases), capture_output=True)
assert result.returncode == 0, result.stderr.decode()
actual = result.stdout
size = len(expected[0])
assert len(actual) == size * len(cases)
for trial, want in enumerate(expected):
    got = actual[trial*size:(trial+1)*size]
    if got != want:
        mismatch = next(i for i,(a,b) in enumerate(zip(got,want)) if a != b)
        raise AssertionError((trial, mismatch, got[mismatch:mismatch+16].hex(), want[mismatch:mismatch+16].hex()))
print(f'Battle background matches original CPU logic in {len(cases)} cases (shared GTE model)')
