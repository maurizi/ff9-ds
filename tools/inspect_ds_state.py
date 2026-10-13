#!/usr/bin/env python3
"""Inspect melonDS 1.1 ARM9 registers and ITCM against a matching ELF.

This diagnoses a saved execution state; it does not establish route correctness.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('state', type=Path)
p.add_argument('--elf', required=True, type=Path)
a = p.parse_args()
data = a.state.read_bytes()
if data[:4] != b'MELN' or struct.unpack_from('<I', data, 8)[0] != len(data):
    p.error('expected a complete melonDS savestate')
sections = {}
pos = 16
while pos < len(data):
    size = struct.unpack_from('<I', data, pos + 4)[0]
    if size < 16 or pos + size > len(data):
        p.error('invalid section length')
    sections[data[pos:pos + 4]] = data[pos:pos + size]
    pos += size
cpu, cp15 = sections[b'ARM9'], sections[b'CP15']
registers = struct.unpack_from('<17I', cpu, 24)
result = {
    'state': str(a.state), 'elf': str(a.elf),
    'registers': {f'r{i}': f'{v:08x}' for i, v in enumerate(registers[:16])},
    'cpsr': f'{registers[16]:08x}',
    'abort_bank': [f'{x:08x}' for x in struct.unpack_from('<3I', cpu, 136)],
    'irq_bank': [f'{x:08x}' for x in struct.unpack_from('<3I', cpu, 148)],
    'cp15_control': f'{struct.unpack_from("<I", cp15, 16)[0]:08x}',
}
# ELF32 section headers identify the original linked ITCM, without a toolchain
# dependency or assuming it lives at a particular file offset.
elf = a.elf.read_bytes()
if elf[:6] != b'\x7fELF\x01\x01':
    p.error('expected a little-endian ELF32 executable')
shoff = struct.unpack_from('<I', elf, 32)[0]
shsize, shnum, shstr = struct.unpack_from('<3H', elf, 46)
headers = [struct.unpack_from('<10I', elf, shoff + i * shsize) for i in range(shnum)]
strings = elf[headers[shstr][4]:headers[shstr][4] + headers[shstr][5]]
for h in headers:
    name = strings[h[0]:].split(b'\0', 1)[0]
    if name == b'.itcm':
        original = elf[h[4]:h[4] + h[5]]
        actual = cp15[28:28 + len(original)]
        # Mutable IRQ/runtime tables can occupy the tail. Report code-region
        # changes separately; callers still must interpret the matching map.
        changes = [i for i, (x, y) in enumerate(zip(original, actual)) if x != y]
        result['itcm_changed_bytes'] = len(changes)
        result['itcm_first_16k_changes'] = [
            {'address': f'{h[3]+i:08x}', 'elf': original[i], 'saved': actual[i]}
            for i in changes if i < 16384]
        break
symbols = {}
for line in subprocess.check_output(['nm', '-a', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
ram = sections[b'NDSG']
for name, count in [('frames', 1), ('g_primPtr', 1), ('g_ot', 1),
                    ('exceptionRegisters', 16), ('port_itcm_first_fault', 21)]:
    address = symbols.get(name)
    if address is not None and 0x02000000 <= address < 0x03000000:
        offset = 20 + address - 0x02000000
        result[name] = [f'{x:08x}' for x in struct.unpack_from(f'<{count}I', ram, offset)]
print(json.dumps(result, indent=2))
