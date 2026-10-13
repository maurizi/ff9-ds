#!/usr/bin/env python3
"""Run the opening-to-Baku PC smoke path on the user's extracted disc.

Usage: python3 tools/battle_smoke.py [build/tests/gx_host.elf]
This is an execution/allocator probe; it does not establish visual correctness.
"""
import argparse, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('exe',nargs='?',default='build/pc/ff9ds')
parser.add_argument('--frames',type=int,default=60000)
parser.add_argument('--dump',type=int,default=0,help='write software-renderer frames under build/frames')
parser.add_argument('--gdb',help='run the chosen executable with a GDB command file')
parser.add_argument('--briefing', action='store_true',
                    help='select Princess Garnet in the briefing quiz and continue past the meeting')
parser.add_argument('--press', action='append', default=[],
                    help='append a scripted BUTTON@FRAME[+FRAMES] press for later exploration')
parser.add_argument('--idle-after', type=int,
                    help='stop scripted inputs at this frame to exercise enemy attacks and party KO')
options=parser.parse_args()
exe=options.exe
args=[exe,'--frames',str(options.frames),'--status','2000']
if options.dump: args += ['--dump',str(options.dump)]
presses=['CROSS@2500','UP@2900+3','CROSS@3000+3']
presses += [f'CROSS@{i}+12' for i in range(6000,13000,1000)]
presses += ['RIGHT@14000+4','UP@14100+80','CROSS@15000+12','CROSS@16000+12','START@20000+12','CROSS@20100+12']
presses += [f'CROSS@{i}+12' for i in range(22000,59000,200)]
if options.briefing:
    # Select the lower answer before confirming each potential quiz window.
    # Keep battle inputs unchanged; the first encounter finishes before 32000.
    presses += [f'DOWN@{i-6}+3' for i in range(32000,59000,200)]
presses += options.press
if options.idle_after is not None:
    presses=[p for p in presses if int(p.split('@')[1].split('+')[0]) < options.idle_after]
for press in presses: args += ['--press',press]
if options.gdb: args=['gdb','-q','-batch','-x',options.gdb,'--args']+args
raise SystemExit(subprocess.run(['tools/docker.sh', *args]).returncode)
