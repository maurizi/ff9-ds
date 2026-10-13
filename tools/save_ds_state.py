#!/usr/bin/env python3
"""Save melonDS slot 8 and copy it only after a fresh, complete write.

Optional matching ELF reports the port frame counter alongside emulated VBlanks.
Those counters permit guest-time comparisons when the emulator is accelerated.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--window', required=True)
p.add_argument('--rom', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--elf', type=Path)
a = p.parse_args()
source = a.rom.resolve().with_suffix('.ml8')
out = a.out.resolve()
if not out.is_relative_to(root / 'build'):
    p.error('savestates must stay under build/')
if out.exists():
    p.error('output already exists')
old = source.stat().st_mtime_ns if source.exists() else None
subprocess.run([sys.executable, str(root / 'tools/xkey.py'), '--hold', '0.5',
                a.window, 'Shift_L+F8'], check=True)
limit = time.monotonic() + 15
while time.monotonic() < limit:
    if source.exists() and source.stat().st_mtime_ns != old:
        data = source.read_bytes()
        if len(data) >= 32 and data[:4] == b'MELN' and struct.unpack_from('<I', data, 8)[0] == len(data):
            end = 16
            while end + 16 <= len(data):
                length = struct.unpack_from('<I', data, end + 4)[0]
                if length < 16:
                    break
                end += length
            if end == len(data):
                break
    time.sleep(0.1)
else:
    sys.exit('No fresh complete slot-8 savestate received; nothing copied')
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(data)
meta = {'path': str(out), 'bytes': len(data)}
if a.elf:
    # melonDS 1.1 NDSG: 16-byte section header, config word, 16 MiB RAM.
    assert data[16:20] == b'NDSG'
    section_end = 16 + struct.unpack_from('<I', data, 20)[0]
    meta['emulated_vblanks'] = struct.unpack_from('<I', data, section_end - 23)[0]
    symbols = subprocess.check_output(['nm', '-a', str(a.elf)], text=True)
    address = next(int(line.split()[0], 16) for line in symbols.splitlines()
                   if len(line.split()) == 3 and line.split()[2] == 'frames')
    assert 0x02000000 <= address < 0x03000000
    meta['game_frame'] = struct.unpack_from('<I', data, 36 + address - 0x02000000)[0]
    meta['elf'] = str(a.elf.resolve())
out.with_suffix(out.suffix + '.json').write_text(json.dumps(meta, indent=2) + '\n')
print(json.dumps(meta))
