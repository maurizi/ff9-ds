#!/usr/bin/env python3
"""Batch direct-entry scenarios and retain each failure, without an emulator.

Run inside the build container. Nonzero game exits (including missing routines)
and timeouts are failures, never hidden by a debugger's successful exit status.
This checks execution only; native DS video/PSX comparisons remain necessary.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('scenarios', nargs='+', type=Path)
    p.add_argument('--exe', default='build/pc/ff9ds')
    p.add_argument('--frames', type=int, default=12000)
    p.add_argument('--timeout', type=float, default=60)
    p.add_argument('--out', type=Path, default=Path('build/validation/scenarios'))
    p.add_argument('--press', action='append', default=[])
    args = p.parse_args()
    if args.frames <= 0 or args.timeout <= 0:
        p.error('frames and timeout must be positive')
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    for index, scenario in enumerate(args.scenarios):
        log = args.out / f'{index:02d}_{scenario.stem}.log'
        command = [args.exe, '--scenario', str(scenario), '--frames', str(args.frames), '--status', '1000']
        for press in args.press:
            command += ['--press', press]
        started = time.monotonic()
        with log.open('wb') as output:
            try:
                completed = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=args.timeout)
                code = completed.returncode
                reason = 'exited'
            except subprocess.TimeoutExpired:
                code, reason = None, 'timeout'
        text = log.read_text(errors='replace')
        passed = code == 0 and f'frame limit {args.frames} reached' in text
        row = {'scenario': str(scenario), 'pass': passed, 'returncode': code,
               'reason': reason, 'seconds': round(time.monotonic()-started, 3), 'log': str(log),
               'fatal': next((line for line in text.splitlines() if 'FATAL:' in line), None)}
        results.append(row)
        print(f"{'PASS' if passed else 'FAIL'} {scenario}: {row['fatal'] or reason}", flush=True)
    (args.out / 'report.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(row['pass'] for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
