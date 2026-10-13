#!/usr/bin/env python3
"""Run a tool with a bounded log; retain startup and latest output on disk.

Usage: bounded_log.py build/validation/emulator.log -- COMMAND [ARGS ...]
Repeated diagnostics cannot consume the workspace's free space.
"""
import argparse
from pathlib import Path
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
p.add_argument('command',nargs=argparse.REMAINDER)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
path=a.log.resolve()
if not path.is_relative_to(root/'build'):
    p.error('logs must remain under build/')
command=a.command[1:] if a.command[:1]==['--'] else a.command
if not command: p.error('a command is required')
path.parent.mkdir(parents=True,exist_ok=True)
if path.exists(): p.error('log already exists')
limit=512*1024
head=bytearray()
tail=bytearray()
total=0
last_write=0
child=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
print(f'logging PID {child.pid} to {path} (at most 1 MiB)',flush=True)
def write():
    with path.open('wb') as f:
        f.write(head)
        if total>len(head):
            f.write(f'\n[bounded log: {total} bytes received; latest output below]\n'.encode())
            f.write(tail)
try:
    while True:
        chunk=child.stdout.read1(65536)
        if not chunk: break
        total+=len(chunk)
        needed=limit-len(head)
        if needed>0: head.extend(chunk[:needed])
        tail.extend(chunk)
        if len(tail)>limit: del tail[:-limit]
        now=time.monotonic()
        if now-last_write>=2:
            write();last_write=now
finally:
    write()
raise SystemExit(child.wait())
