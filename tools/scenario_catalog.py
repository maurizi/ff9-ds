#!/usr/bin/env python3
"""List encounter IDs and enemy names directly from a US disc's FF9.IMG.

Does not require an emulator or an existing playthrough. Names identify the
enemy types in a bundle; story flags can still choose different formations.
"""
import argparse
import json
import struct
from pathlib import Path

SECTOR = 2048


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def block(data, kind):
    if data[:1] != b'\xdb':
        raise ValueError('encounter is not a DB container')
    for i in range(data[1]):
        entry = 4 + i * 4
        value = u32(data, entry)
        if value >> 24 != kind:
            continue
        base = entry + (value & 0xffffff)
        count = data[base + 1]
        if not count:
            raise ValueError('empty DB block')
        offsets = (base + 4 + count * 2 + 3) & ~3
        payload = offsets + (u32(data, offsets) & 0xffffff)
        if not 0 <= payload < len(data):
            raise ValueError('DB payload outside encounter')
        return payload
    raise ValueError(f'missing DB block {kind}')


def catalog(path, disc):
    results = []
    with open(path, 'rb') as image:
        header = image.read(SECTOR)
        if header[:4] != b'FF9 ' or u32(header, 8) <= 5:
            raise ValueError('not an FF9 archive with encounter directory')
        kind, count, table_sector, _ = struct.unpack_from('<4I', header, 16 + 5 * 16)
        if kind != 2:
            raise ValueError('unsupported encounter directory format')
        image.seek(table_sector * SECTOR)
        entries = image.read((count + 1) * 8)
        for i in range(count):
            encounter, sector = struct.unpack_from('<2I', entries, i * 8)
            end = u32(entries, (i + 1) * 8 + 4)
            if end <= sector or end - sector > 4096:
                raise ValueError(f'invalid encounter extent {encounter}')
            image.seek(sector * SECTOR)
            data = image.read((end - sector) * SECTOR)
            try:
                params, text = block(data, 16), block(data, 6)
                enemies = []
                for j in range(data[params + 2]):
                    offset = struct.unpack_from('<H', data, text + 4 + j * 4)[0]
                    start = text + 4 + offset
                    stop = data.index(255, start)
                    enemies.append(''.join(' ' if c == 15 else chr(c + 49)
                                           if 16 <= c <= 94 else f'<{c:02x}>'
                                           for c in data[start:stop]))
            except (ValueError, IndexError, struct.error) as exc:
                raise ValueError(f'encounter {encounter}: {exc}') from exc
            results.append({'disc': disc, 'id': encounter, 'enemies': enemies})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--disc', type=int, choices=range(1, 5), required=True)
    parser.add_argument('--find', default='', help='enemy name substring')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        rows = catalog(args.image, args.disc)
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(1, f'{exc}\n')
    rows = [row for row in rows if args.find.casefold() in ' '.join(row['enemies']).casefold()]
    if args.json:
        print(json.dumps(rows, indent=2))
    else:
        for row in rows:
            print(f"disc {row['disc']}  battle {row['id']:3}  {', '.join(row['enemies'])}")


if __name__ == '__main__':
    main()
