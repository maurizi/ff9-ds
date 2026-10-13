#!/usr/bin/env python3
"""Capture a paused PCSX-Redux scene directly from its GPU, without desktop UI.

The decomp's tools/emu.py must already have started the emulator. Advances two PSX
VBlanks per video frame, producing a 30 fps clip in emulated time. Use title/field
state and memory-card context to match a DS capture; FMV durations differ in the port.
"""
import argparse
import importlib.util
import re
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--out', required=True)
    parser.add_argument('--press', action='append', default=[], help='button chord at a clip-relative even VBlank, e.g. CROSS@6+6')
    parser.add_argument('--emu-tool', type=Path, default=Path(__file__).resolve().parents[2] / 'ff9-decomp/tools/emu.py')
    args = parser.parse_args()
    if not 0 < args.seconds <= 30:
        parser.error('--seconds must be between 0 and 30')
    root = Path(__file__).resolve().parents[1]
    output = Path(args.out).resolve()
    if not output.is_relative_to(root / 'build') or output.suffix != '.mp4':
        parser.error('output must be an MP4 under this project\'s build/')
    if output.exists():
        parser.error('output already exists; choose a new capture name')
    spec = importlib.util.spec_from_file_location('ff9_emu', args.emu_tool)
    if spec is None or spec.loader is None:
        parser.error('cannot load decomp emulator tool')
    emu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(emu)
    if emu.is_running():
        parser.error('pause PCSX-Redux before capturing')
    presses = {}
    for value in args.press:
        match = re.fullmatch(r'([A-Za-z0-9_,+]+)@(\d+)(?:\+(\d+))?', value)
        if not match:
            parser.error('invalid press: use CROSS@6+6')
        names = [emu.BUTTON_ALIASES.get(n.upper(), n.upper())
                 for n in match[1].replace('+', ',').split(',')]
        frame, hold = int(match[2]), int(match[3] or 2)
        if any(n not in emu.BUTTONS for n in names) or frame % 2 or frame >= args.seconds*60 or hold < 1:
            parser.error('press needs valid buttons, an even frame inside the clip, and a positive hold')
        presses.setdefault(frame, []).append((names, hold))
    start_frame = emu.bridge('status')['frame']
    output.parent.mkdir(parents=True, exist_ok=True)
    encoder = subprocess.Popen(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'image2pipe',
                                '-framerate', '30', '-vcodec', 'png', '-i', '-', '-an',
                                '-vf', 'pad=ceil(iw/2)*2:ceil(ih/2)*2', '-c:v', 'libx264',
                                '-crf', '18', '-pix_fmt', 'yuv420p', '-n', str(output)], stdin=subprocess.PIPE)
    try:
        for i in range(round(args.seconds * 30)):
            for names, hold in presses.get(i*2, []):
                emu.bridge('hold', names, hold)
            emu.run_frames(2)
            encoder.stdin.write(emu.http('/screen/still', timeout=10))
    finally:
        try:
            emu.bridge("releaseAll")
        finally:
            encoder.stdin.close()
            code = encoder.wait()
    if code:
        raise SystemExit(code)
    output.with_suffix('.mp4.json').write_text(json.dumps({
        'start_vblank': start_frame, 'vblanks': round(args.seconds*30)*2,
        'presses': args.press, 'fps': 30, 'time_base': 'emulated PSX VBlanks'
    }, indent=2)+'\n')
    print(output)


if __name__ == '__main__':
    main()
