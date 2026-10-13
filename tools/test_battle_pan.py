#!/usr/bin/env python3
"""Check the built battle panning function's unit-ID/native-pointer dispatch."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / 'build/portified/src/ovl_0b/ovl_0b_2.c').read_text()
start = source.index('s32 func_800E976C(s32 arg0) {')
end = source.index('\n}\n', start) + 3
function = source[start:end]
probe = r'''
#define M2CTX
#define INCLUDE_ASM(...)
#define INCLUDE_RODATA(...)
#define printf psx_printf_declaration
#include "ovl_0b.h"
#undef printf
#include "port_hw.h"
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
static Unk800F78A4 context;
Unk800F78A4 *D_800F78A4 = &context;
static MATRIX matrix;
static SVECTOR point = {100, 0, 1000, 0};
static int lookups;
void func_800E750C(s32 id, s16 *out) {
    assert(id >= 0 && id <= 17);
    lookups++;
    out[0] = point.vx; out[1] = point.vy; out[2] = point.vz;
}
'''
probe += function
probe += r'''
int main(void) {
    context.unk14 = &matrix;
    matrix.m[0][0] = matrix.m[1][1] = matrix.m[2][2] = 4096;
    matrix.t[2] = 4096;
    gte_ctc2(24, 160u << 16); gte_ctc2(25, 112u << 16); gte_ctc2(26, 256);
    int expected = func_800E976C(0);
    assert(lookups == 1 && expected > 128);
    for (int id = 0; id <= 17; id++) {
        lookups = 0;
        assert(func_800E976C(id) == expected && lookups == 1);
    }
    lookups = 0;
    assert(func_800E976C(-1) == 128 && lookups == 0);
    assert(func_800E976C((s32)&point) == expected && lookups == 0);
    SVECTOR local = point;
    assert(func_800E976C((s32)&local) == expected && lookups == 0);
    SVECTOR *ds = mmap((void *)0x02000000, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(ds == (void *)0x02000000);
    *ds = point;
    assert(func_800E976C((s32)ds) == expected && lookups == 0);
    munmap(ds, 4096);
    puts("Battle panning dispatch passed: 18 unit IDs, sentinel, static/stack/DS pointers");
}
'''
path = root / 'build/tests/battle_pan_dispatch.c'
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(probe)
args = ['tools/docker.sh', 'gcc', '-m32', '-fno-pie', '-no-pie', '-std=gnu17',
        '-O1', '-g', '-fsanitize=address,undefined', '-Wno-builtin-declaration-mismatch',
        '-Iport/include', '-Idecomp/include', 'build/tests/battle_pan_dispatch.c',
        'port/psx/gte.c', '-o', 'build/tests/battle_pan_dispatch']
subprocess.run(args, cwd=root, check=True)
subprocess.run(['tools/docker.sh', 'build/tests/battle_pan_dispatch'], cwd=root, check=True)
