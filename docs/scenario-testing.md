# Direct scenario testing

Launch a field or encounter through the current build's normal engine and
asset loaders. No playthrough, emulator CPU snapshot, or old executable is
needed. This supports testing later discs as soon as their loaders work.

Find an encounter in your extracted disc:

```sh
python3 tools/scenario_catalog.py disks/us/disc3/FF9.IMG --disc 3 --find Ark
# disc 3  battle 0  Ark
```

Run a fixture on the host, retaining the game's actual failure exit code:

```sh
tools/docker.sh make -j16 pc
tools/docker.sh build/pc/ff9ds --scenario tests/scenarios/disc3_ark.cfg --frames 12000 --status 1000
tools/docker.sh python3 tools/scenario_smoke.py tests/scenarios/*.cfg
```

The batch runner writes individual logs and `report.json` under
`build/validation/scenarios/`. A missing routine, crash, timeout, or failure to
reach the requested frame limit is a failure. Passing this execution check
does not establish visual correctness or DS performance.

For native DS tests, copy the fixture to the SD filesystem and compile its
path into the test ROM:

```sh
tools/docker.sh make -j16 ds AUTORUN=1 ITCMGUARD=1 SCENARIO=sd:/ff9ds/scenarios/disc3_ark.cfg
```

The selected raw disc must be present at `sd:/ff9ds/raw/FF9 CD3.img`.
melonDS folder sync does not grow an existing auto-sized SD image reliably
when another disc is added. Use a newly created SD image with a folder
containing the selected disc, or an image sized for all desired discs.
Use one emulator at a time. Capture screenshots and short videos on DS and
the matching PSX encounter; emulator saves remain useful for inspecting a
precise failure in the same build.

Fixtures use `key=value`, with comments beginning with `#`:

| Key | Meaning |
| --- | --- |
| `kind` | Required `field` or `battle` |
| `disc` | Disc 1–4; defaults to 1 |
| `field` | Field ID; defaults to opening field 70 |
| `battle` | Required encounter ID for battles, resolved against that disc |
| `party` | Four character indices, 0–8 or -1 for empty; first slot occupied |
| `level` | Optional level 1–99, rebuilding stats and healing |
| `hp.SLOT` | Override current HP after leveling, bounded by maximum HP |
| `equip.SLOT` | Five equipment item IDs, 0–255 |
| `item.ID` | Set inventory quantity 0–99, preserving other items |
| `story` | Set the 16-bit event story counter |
| `event.OFFSET` | Set an individual event byte, offsets 0–0x7ff |
| `characters` | Exact 9 × 0x90-byte canonical character records |
| `events` | Exact 0x800-byte canonical event variables |
| `seed` | Override initialization RNG seed; defaults to 1 |

Blob paths are interpreted relative to the process working directory; use
absolute SD paths on DS. Imported records contain no runtime pointers. Blob
values are applied first, then explicit overrides. Without blobs, the normal
New Game initializer supplies the baseline. The Ark and Meltigemini fixtures
are synthetic combat setups, not canonical late-game saves. Enemy names list
types available in a bundle; story flags can select formations or scripts.

Current limits: battle fixtures exercise isolated encounters; returning to a
fully restored parent field/world state is not yet implemented. Field story
and spawn requirements still need appropriate fixture flags. RNG seeding
does not by itself guarantee deterministic wall-clock/input behavior.
Canonical save-data extraction and scripted action/phase assertions will
extend these fixtures; loading a boss is not equivalent to covering all of
its attacks, victory, defeat, or cutscene transitions.

Verified: opening field 70 entered directly on the host; disc-3 Ark entered
directly on both host and melonDS, revealing optional null default-motion
handling and missing battle effect emitters without playing the opening.
The two Ark emitters D4A30 and D45FC now have native equivalents that pass
original-MIPS packet comparisons. Ark subsequently reaches an unresolved
CD-loaded MIPS effect module: its header pointer at PSX 801E7700 is 801E9058,
initializer is zero, and first effect callback is 801E8178. The generated
resident overlay relocations do not cover this module. The 12000-frame batch
therefore reports Ark as a crash, and Meltigemini as missing D3ED4; neither
boss is claimed to pass. Reports live in
`build/validation/scenario_emitters_batch/`. The matched native test ROM/ELF
is `ff9ds_scenario_ark_emitters`, with a fresh-boot capture in
`build/validation/ds_direct_disc3_ark_fresh_boot.mp4`.

The subsequent `effect_vm.c` bridge executes integer/GTE glue from these
CD-loaded effect modules and calls their native battle-engine exports. The
reviewed pointer-argument masks preserve scalar flags and optional NULLs;
D044C's second argument is an output pointer carried as `s32`. Resident
callbacks keep their native entry points. The bridge passes 300 differential
integer/branch-delay/GTE-model cases, 60 cases of Ark's original initialization
code, and the native 16-word argument test under ASan/UBSan. It does not yet
claim complete MIPS-I instruction/load-delay coverage or support for every
module/export signature. Ark now reaches missing D5C30 on the host; the
original dynamic-header crash is passed, but its full attack remains unported.
Fresh ARM validation reproduces that later missing D5C30 stop at frame 1595
(`ds_ark_effect_vm_probe.ml8`, matching `ff9ds_ark_effect_vm` ROM/ELF), with
the first-fault marker zero and no instruction-region corruption. The existing
60000-frame opening-to-briefing host route also passes with this bridge.

## Subsequent Ark effect coverage

D5C30's circular-chain ribbon emitter and the shared DFF5C/DFF64 particle
integrator now have native equivalents. The differential suite compares
context bytes, packets, OT links and raw production GTE state against original
MIPS in 400 randomized cases under ASan/UBSan. D3ED4's textured effect quads
also pass the expanded 400-case quad/sprite suite (shared GTE model).

Further Ark failures exposed two portability details: the export interface's
model-pool initialization arguments are pointers into the loaded module; and
E1534 reuses effect descriptors whose pointer fields carry the PSX sign bit
after relocation. Native pointers lack that bit, so repeated initialization
added the base twice. Reviewed export masks now map model-pool arguments, and
counted descriptor patches only relocate values still within the offset range.

Fresh DS execution reaches frame 3051, executes attacks and reduces party HP,
then stops at missing D4228. The matching `ff9ds_ark_ribbons_particles` ROM/ELF
and `ds_ark_ribbons_particles_combat.ml8` retain the failure. Its first-fault
marker is zero and the ITCM instruction region matches the ELF. Videos:
`ds_ark_ribbons_particles_boot.mp4` and
`ds_ark_ribbons_particles_combat.mp4`. This does not prove the entire boss,
exit paths, visual fidelity, or performance. The host batch still reports Ark
and its low-HP variant as missing D4228, and Meltigemini as a later segmentation
fault (`scenarios_after_ribbons/report.json`).

## Matching original PSX encounters

The original PSX also enters Ark directly from a disc-3 field baseline using
the host fixture's pointer-free character/event records and ordinary battle
resources. `tools/psx_battle_fixture.py` reads encounter/background sectors
from the original disc, unwinds the original field before importing synthetic
story data, and stops at the resident dispatcher before the battle loader.
It changes runtime test data, preserving executable bytes.

Start Redux with disc 3 paused, then run:

```sh
python3 tools/psx_battle_fixture.py tests/scenarios/disc3_ark.cfg \
  --characters build/validation/ark_fixture_characters.bin \
  --events build/validation/ark_fixture_events.bin \
  --base-state disc3_scenario_field_ready
```

Export these exact-size blobs after `port_scenario_bootstrap` returns in the
host build: `GameData.characters` (9 x 0x90) and `D_8007AEF0` (0x800). The
referenced local baseline was obtained from the existing disc-3 Pandemonium
memory-card save, without a playthrough. A cold Redux BIOS must boot before
restoring it; otherwise this instance restores RAM/VRAM but remains in a BIOS
loop. The tool boots a cold BIOS before its own baseline restore. Do not start
Redux with `--state` before booting for this path. A pause in a currently live,
initialized field also works without `--base-state`.

The tool is runtime-validated after warm boot (`psx_fixture_warm_boot.log`,
`psx_fixture_warm_boot.png`) and from a cold paused start through automatic
boot (`psx_fixture_auto_boot.log`, `psx_fixture_auto_boot.png`), independently
of its offline range check. The
original entrance/combat video is `psx_direct_ark_entrance_combat.mp4`; the
Photon HUD screenshot is `psx_direct_ark_hud_reference.png`. Party/story data
match; input timing and RNG phases are not synchronized, and inventory is not
imported by this PSX helper.

## Cached HUD text and subsequent effect interfaces

The earlier screenshot-based diagnosis of stale/overlapping DS name text was
incorrect. Replaying the saved PSX VRAM through the scaler reproduces the active
DS bitmap exactly, including the name row (`ds_ark_scaler_replay.json`: zero
different pixels). Cached battle names and HP/MP sprites bypassed the font
coverage matcher and received nearest sampling instead. The matcher now includes
the reviewed atlas bounds for cached names, cached values, and direct digits.
Its positive/negative UV tests pass under ASan/UBSan. A fresh DS state at frame
2483 records 64 coverage rectangles in each frame list, including four names
and eight cached values, with no first-fault marker or ITCM corruption.
`ark_hud_same_frame_comparison.png` compares old/new filtering on identical
source pixels; `ds_ark_hud_coverage_boot.mp4` and
`ds_ark_hud_coverage_combat.mp4` retain live animation. This establishes coverage
of these sprites, not complete visual fidelity or acceptable performance.

D4228/D437C now emit native projected sprites/points and pass 600 original-MIPS
CPU/packet cases using a shared GTE model. Subsequent failures exposed raw PSX
positions and output buffers inside loaded modules, export-table entries beyond
the former 160-entry scan, and six/nine-argument callbacks. The bridge scans
216 function entries (data follows), decodes reviewed pointer arguments while
preserving scalar bit patterns, and supports up to 16 o32 callback arguments.
Local descriptor reads and D1494 buffer accesses decode pointers without changing
the module's stored pointer representation. Tests exercise late exports,
six/nine incoming arguments, zero-padding of native arguments, and 16-word
outgoing calls. Fresh native DS execution with these changes reaches frame 4251
and stops at missing D57D0, with no first-fault marker or changes to the first
16 KiB of ITCM (`ds_ark_trail_writer_stop.ml8` and inspection JSON, matching
`ff9ds_ark_trail_writer` ROM/ELF). Boot and combat videos with the same prefix
retain animation. This validates the prior pointer/callback fixes along this
route, not all module interfaces.

CF5A4 now writes circular trail positions/widths; 400 randomized cases compare
the entire buffer and return value with original MIPS, including wraparound,
byte counter rollover, and raw/native input pointers. The host Ark route now
passes the prior failures and stops at missing D57D0
(`scenarios_trail_writer/report.json`). The encounter and later content remain
incomplete; neither this progress nor a passing build proves the boss playable.

D57D0 now emits the single textured effect rectangle, preserving UV arithmetic,
context scratch writes, the existing MAC0 cull decision, depth bucket limits,
OT links and optional color fade. Its original-MIPS differential cases expand
the quad suite to 700 cases using a shared GTE model. Both targets build. The
host proceeds past frame 6000 before a raw output pointer to E750C causes a
segmentation fault (`pc_ark_d57d0_fault.log`); D57D0's native DS rendering still
requires validation.

Reviewed unit-position exports now decode position/rotation/vector buffers at
table offsets 1F8, 1FC, 200, 204, 20C, 214, 2A0, 2A8 and 2B8. Model draw
exports at 60, 64, 28C and 31C also decode their first three arguments: the
third is declared `s32` in wrappers but forwarded as a scale vector to DCA68.
Summon bone output buffers at 254/290 receive the same treatment. Fifteen
synthetic export cases verify mapping, native writes and unchanged scalar IDs.
The existing 360-case effect VM suite and both target builds pass. Both Ark
fixtures advance to a different failure: BFAF4 dereferences a null `curCmd`
while processing status bit 0x400 (`pc_ark_positions_models_fault.log`).
This interface batch still requires fresh native DS validation.
