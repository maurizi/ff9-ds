#!/usr/bin/env python3
"""Record one emulator window; all game-derived output must stay in build/.

Examples:
  tools/capture.py --window melonDS --seconds 8 --out build/validation/ds_fade.mp4
  tools/capture.py --window PCSX-Redux --seconds 8 --out build/validation/psx_fade.mp4
Requires ffmpeg and xwininfo. Records wall-clock time at 30 fps, without audio.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--window', required=True, help='unique emulator title substring, or hexadecimal window ID')
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 60:
        parser.error('--seconds must be between 0 and 60')
    root = Path(__file__).resolve().parent.parent
    output = Path(args.out).resolve()
    if not output.is_relative_to(root / 'build'):
        parser.error('game-derived captures must be written under this project\'s build/')
    if output.suffix != '.mp4':
        parser.error('--out must end in .mp4')
    if output.exists():
        parser.error('output already exists; choose a new capture name')
    env = dict(os.environ)
    if not env.get('DISPLAY'):
        sockets = sorted(Path('/tmp/.X11-unix').glob('X*'))
        if not sockets:
            parser.error('no X display found')
        env['DISPLAY'] = ':' + sockets[0].name[1:]
    if not env.get('XAUTHORITY'):
        auth = Path(f'/run/user/{os.getuid()}/gdm/Xauthority')
        if auth.exists():
            env['XAUTHORITY'] = str(auth)
    if args.window.startswith('0x'):
        window = str(int(args.window, 16))
    else:
        tree = subprocess.check_output(['xwininfo', '-root', '-tree'], env=env, text=True)
        # Match application windows, excluding the window manager's frames and tiny helper windows.
        pattern = r'\s*(0x[0-9a-f]+) "([^"]+)": \("[^"]+" "([^"]+)"\)\s+(\d+)x(\d+)'
        matches = [m[0] for m in re.findall(pattern, tree)
                   if args.window.casefold() in m[1].casefold()
                   and m[2] != 'mutter-x11-frames' and int(m[3]) > 100 and int(m[4]) > 100]
        if len(matches) != 1:
            parser.error(f'expected one matching application window, found {len(matches)}; use its hexadecimal ID')
        window = str(int(matches[0], 16))
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'x11grab',
                    '-framerate', '30', '-window_id', window, '-i', env['DISPLAY'],
                    '-t', str(args.seconds), '-vf', 'pad=ceil(iw/2)*2:ceil(ih/2)*2',
                    '-an', '-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p',
                    '-n', str(output)], env=env, check=True)
    print(output)


if __name__ == '__main__':
    main()
