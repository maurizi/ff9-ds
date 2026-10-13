# Porting notes: PS1 behaviours the decomp's C relies on

Matching C reproduces the PS1 binary exactly, so it also reproduces things that only work because the code runs on
a MIPS R3000 with PS1 memory. Source audits and native runtime probes identify these dependencies. Each class below has a general fix, and an
upstream request where the decomp can make the C say what it means without changing the PS1 output.

| class | example | why it works on the PS1 | port fix | upstream |
|---|---|---|---|---|
| `void` function whose result is used | `D_80067948 = func_800AD44C(...)` | `$v0` still holds the last callee's result | real return type | fixed at the source (decomp `tools/void_returns.py`, 15ec54c) |
| argument passed through in a register | `func_800AA3F4(void)` calls `func_800AAEF8()` | `$a0` still holds the caller's argument | the port defines `REG_PASSTHROUGH(x)` as `x` | fixed at the source (decomp `tools/arg_check.py`, 1b41477) |
| pointer-ness tested by sign | `if ((s32)g >= 0)` = "still a file offset" | RAM pointers are 0x80xxxxxx, negative | the port defines `IS_OFFSET(x)` as "not a pointer into psx_ram" | `IS_OFFSET()` at all 16 sites (decomp 386d31f) |
| pointer-ness tested against 0x80000000 | `if (p > 0x80000000)` | same | `PORT_IS_RAM_PTR()` (patch) | listed in R4 |
| read/write through NULL | AKAO reads, movement collision reads, optional battle texture-animation controls | low RAM is readable and writable | typed, single-evaluation `PSX_NZ()` at reviewed dereferences | annotate original behaviour; retain matching code |
| constant passed as a pointer | `CdControlB(CdlSetmode, (u8 *)0x80, 0)` | reads physical address 0x80 | `PSX_PTR()` in the library call | noted in R4 |
| pointer through a 26-bit script value | `o = (Unk8004AB64 *)func_80052D40()` | 0x00xxxxxx is the RAM mirror | `PORT_SCRIPT_PTR()` (patch), `SCRIPT_PTR_RESTORE` in src_equiv | done for src_equiv |
| busy-wait on the vblank counter | `while (VSync(-1) - t < n)` (`func_800B2C80`) | the counter moves in real time | a run of `VSync(-1)` polls lets a frame pass | none needed |
| interrupt-driven progress | the movie frame counter, CD sectors, AKAO ticks | hardware interrupts | virtual interrupts at VSync and in libcd (`port/psx/irq.c`, `cd.c`) | none needed |

The decomp checks the first two classes from the asm (`tools/void_returns.py`, `tools/arg_check.py`; both report 0),
so new decomp work doesn't reintroduce them. Patches live in `config/patches.txt` as exact text with an expected count, so one that no longer applies fails the
build instead of silently doing nothing. Remove a patch once the decomp fixes its site.

## Debugging recipe (PC build)
```
tools/docker.sh make -j16 pc
tools/docker.sh build/pc/ff9ds --frames 20000 --status 1000 --watch 800AB3C8:1 \
    --press CROSS@2500 --press UP@2900+3 --press CROSS@3000+3      # title -> New Game -> field 70
tools/docker.sh tools/pcdebug.sh <same args>                       # crash: fault address, line, backtrace
```
Most crashes so far were found by following the bad value back with gdb watchpoints (`watch -l`) on PS1 globals,
which are ordinary symbols in the port.

## Running the DS build in an emulator
- **melonDS, DSi mode** (Flatpak `net.kuribo64.melonDS`), the main target: `flatpak run net.kuribo64.melonDS
  build/ff9ds.nds`. It needs your own DSi dumps (ARM9i/ARM7i BIOS, DSi firmware, NAND) and DS BIOS/firmware, kept
  outside the repo (`~/.var/app/net.kuribo64.melonDS/data/melonDS-dsi/`). Config:
  `~/.var/app/net.kuribo64.melonDS/config/melonDS/melonDS.toml` with `Emu.ConsoleType = 1`,
  `Emu.ExternalBIOSEnable = true`, `Emu.DirectBoot = true`, the `[DS]`/`[DSi]` paths, and `[DSi.SD]` with folder sync
  onto `build/sdroot/` (which holds `ff9ds/raw/FF9 CD1.img`, a link to the disc image). The game reads `sd:/ff9ds`.
  Verified 2026-10-11: the title -> New Game script reaches field 70 (mode 1, ovl_0A) at frame ~5000.
- melonDS doesn't exit on SIGTERM: close it with `pkill -9 -x melonDS` after a run (never `pkill -f`, which matches
  its own shell). Use dotted keys under `[Gdb]` if you set any: melonDS can't save `[Gdb]` + `[Gdb.ARM9]` table pairs.
  The Flathub build has no GDB stub.
- **melonDS, DS mode (4 MB)**: runs the game logic without the VRAM/SPU shadows, only up to the first overlay the
  game restores from its VRAM cache (after New Game). DLDI folder sync (`[DLDI]`) gives it the SD card.
- **no$gba 2.8 under Wine**, fallback with a debugger: `tools/nogba.sh setup DIR` (copies your no$gba DSi setup and
  builds a FAT32 `DSI-1.SD` with the disc image), `tools/nogba.sh run`. It boots the DSi menu from the NAND and
  starts the cartridge with an XTest key press (`tools/xkey.py`).
- DeSmuME: no DSi mode, and its R4 slot-1 emulation stalls on this ROM; not used.
- For manual validation, this workstation’s melonDS keyboard bindings are arrows, Z (DS A / PSX Cross),
  X (DS B / PSX Circle), Return (Start), A/S (X/Y) and Q/W (L/R). A fixed-frame input script can miss
  title-menu readiness as port timing changes; inspect the cursor and confirm New Game manually.
- `make ds AUTORUN=1 AUTOPRESS="CROSS@2500,UP@2900+3,CROSS@3000+3"` runs the title-to-New-Game script with no input.
  This historical script is timing-sensitive; it did not reliably confirm New Game with the current GX path.
  The status line at the bottom of the console shows frame, mode, overlays and libcd state.

## DS hardware graphics (M2, 2026-10-12)

`port/ds/render_gx.c` consumes the shared GP0 decoder. `make ds` uses GX; `make ds SOFTDISPLAY=1` uses the software
reference GPU. Runtime option changes are tracked by `build/ds/runtime-config`, so switching paths does not
require deleting objects. The GX console reports the last frame's submitted polygons and cumulative dropped
primitives. Empty VBlank submissions retain the last display list.

Full 256×256 pages exhausted texture VRAM at the title and field entry. The cache now converts the used region,
aligned to 16 texels and rounded to power-of-two dimensions, and shares palettes for the same CLUT generation.
Texture-window state is baked into those regions. Current and preceding submissions pin allocations so later
uploads cannot overwrite textures still being rasterised. Failed allocation evicts older unpinned entries;
if the remaining working set cannot fit, the primitive is dropped and counted. Both the 2048-polygon and
6144-vertex hardware limits must be respected (the port reserves 48 polygons).
The GX path now assigns A/B/C/D to textures (512 KiB). The debug console uses bank H
instead of C: its font and tile map fit within 32 KiB. The previous 384 KiB layout
exhausted allocations on the opening name-entry screen. Emulator validation is recorded below.

Cache lookup uses hash chains. Dirty tracking covers each converted region rather than the entire page;
RAM copies of converted pixels/palettes detect unchanged contents inside dirty blocks, avoiding redundant
allocations when neighbouring framebuffer or CLUT entries change. Dialogue clipping is independent of the
framebuffer origin: `PutDrawEnv` selects the target, while subsequent GP0 areas clip sprites and polygons.

VRAM generation tracking wraps at 1024×512 and includes every block touched by an unaligned 256-colour CLUT.
`tools/docker.sh make test-gx` checks wrapping, palette invalidation/sharing, region conversion, nonzero
transparent indices, texture windows, allocation failure, pinned lifetimes, unchanged dirty contents, empty frames,
vertex limits, dialogue/polygon clipping and CPU sprite compositing (subtraction, modulation, STP,
transparent texels and geometry invalidation) under ASan/UBSan. It mocks libnds and does not validate the hardware rasteriser.

Validation so far: PC reference renderer reaches field mode through 6000 scripted frames; melonDS DSi shows the
title/menu and the first room with Zidane. Title frames have zero drops; field videos exposed missing glyphs,
background flicker and repeated texture allocation. Glyph clipping and unchanged-content reuse have been fixed;
field fidelity/performance remain under investigation. M2 remains
in progress: offscreen DRAWENVs are skipped, additive blending and subtractive polygons are approximated,
textured STP is treated at polygon granularity outside the exact sprite path, and the fixed viewport targets 320×224. Battle/world-map scenes,
real hardware and performance still need validation. Game-derived screenshots stay under `build/`.

Tracing the PC software GPU at the first-room dialogue identifies textured subtractive (ABR=2) sprites with
RGB 64,64,64 over the background. A constant black-alpha overlay cannot reproduce PSX subtraction. GX now tracks
known rectangle pixels in a 320×224 CPU shadow, invalidating polygon/line bounds and approximated blends. An ABR=2
rectangle over entirely known pixels is composited with PSX modulation, per-texel STP and saturating channel
subtraction, then drawn as an opaque direct texture. Identical composites share the existing pinned cache.
Uncertain areas retain the approximation; this is not an exact general-purpose blend implementation. Full-frame
fills and each new GX submission initialise the shadow from the GX clear colour. The shadow costs about 210 KiB;
CPU work and texture VRAM use still require real-hardware profiling.
The console’s `blend` line counts exact sprite composites and rectangle fallbacks cumulatively.

The exact sprite path was checked in melonDS at "Sure is dark...": `build/validation/ds_first_room_subtraction.png`
and the eight-second `ds_first_room_subtraction.mp4` show the black room background seen in the verified PSX
capture, with no dropped primitives in that run. `psx_first_dialogue_advance.mp4` and
`ds_first_dialogue_advance.mp4` capture dialogue closure and Zidane moving to "Guess nobody’s here yet...".
The later stills are `psx_dialogue_advanced.png` and `ds_dialogue_advanced_current.png`; both show a small lit area
around Zidane. The clips expose different transition timing, so they are not a lockstep timing match. Geometry
blend fallback and character shading still need closer comparison. The PSX state `ds_port_first_room_verified`
is saved in the decomp’s portable Redux directory for repeatable checks.

## Comparing with the original PSX game

Use the original game in PCSX-Redux via `../ff9-decomp/tools/emu.py`, as well as the port's PC software renderer.
Record the disc, memory-card contents, selected menu entry, scene/field ID and input sequence. The installed Redux
card 1 has six saves and preselects Continue. A port with no card saves can preselect New Game, so a fixed Up/Cross
sequence is not a portable comparison recipe. Inspect the selected entry before confirming it.

Verify actual field entry, not just a movie or a resident overlay. On 2026-10-12 a live PSX boot reached field 70
(GameState.mode=1, GameData.fieldId=70) and the visible "Sure is dark..." dialogue. Its screenshot is
`build/validation/psx_first_field.png`; the room is substantially darker than in the DS capture. This single
capture does not establish that the background should be completely black throughout the scene. Compare
matched animation frames before making that claim. Older title save-state input did not behave like the live boot;
use the verified live scene for this comparison. Port FMVs are currently two-frame placeholders, so absolute
frame numbers and movie durations cannot be matched to the PSX.

Capture short videos for fades, transitions, cursor movement and character animation. Commands:
```
python3 tools/capture.py --window melonDS --seconds 8 --out build/validation/ds_scene.mp4
python3 tools/psx_clip.py --seconds 4 --out build/validation/psx_scene.mp4
```
The first records one X window at 30 fps in wall-clock time (including the DS debug console). The second requires
paused Redux and advances two PSX VBlanks per frame, capturing directly from the GPU at 320×224 without desktop
debug windows. It leaves Redux paused. Do not issue emulator commands concurrently with the PSX clip recorder.
`build/validation/psx_first_room_idle.mp4` is a verified four-second, 120-frame baseline. Compare scene milestones,
occlusion, lighting, text, actor poses and motion; retain differences in notes instead of declaring success from
a single favourable frame.

## Candle sequence validation (2026-10-12)

Moving toward the opening room’s candle exposed a NULL collision-result read in the draft
`func_800475D8`: PC SIGSEGV at 0x0F, DS data abort. The original PSX assembly reads `0xF(s3)`
at 0x800478D8 before its NULL check at 0x800478FC. The counted patch in `config/patches.txt`
maps just that read through `PSX_NZ(o)`; `o` remains NULL for the collision test. The live PSX
byte at 0x0F is zero, matching the port’s low-RAM shadow for this calculation. See decomp finding F2.

Reference states in the portable Redux directory: `ds_port_before_candle` (second dialogue),
`ds_port_candle_ready` (question bubble beside the candle), and `ds_port_light_candle_choice`
(the "Light the candle / Cancel" choice). These are reached from a verified live New Game boot.

The current viewport reduces 320×224 PSX pixels to 256×179 DS pixels. Existing glyph sprites
are scaled with the scene, losing thin strokes; this is separate from missing primitives
caused by texture allocation or clipping. Native UI/font layout remains the planned M5 work
in `dual-screen.md`. Enlarging melonDS does not restore the lost glyph pixels.

The 512 KiB GX build was booted fresh in melonDS (old savestates contain old ARM code
and cannot validate a rebuilt renderer). New Game selection was inspected explicitly.
The opening candle choice, lighting animation and name entry all retained a zero cumulative
drop count, unlike the preceding 384 KiB build's 16,869 drops at name entry.
`build/validation/ds_bank_fix_candle_choice.mp4`, `ds_bank_fix_candle_animation.mp4`
and `ds_bank_fix_name.png` record these milestones. The PSX name reference is
`psx_name_reference.png` from `ds_port_name_entry`. The HELP/NAME labels look clipped in GX despite zero drops. The PC software renderer and
PSX show full labels. A trace shows ordinary 7×7 sprites inside full-screen clipping bounds;
the saved DS cache contains complete HELP glyph pixels (`ds_help_cache_pixels.png`).
This points to lost single-pixel strokes during viewport downscaling rather than missing
source glyphs. Native font/UI rendering is still needed for readable small labels.
The dark-room rectangular shading artifact also persists around Zidane.

Name confirmation preserves "Zidane" and reaches the fully lit "It’s me, Zidane!" greeting.
`ds_candle_room_reveal_gx512.mp4` captures name confirmation and the return to the room;
`ds_candle_greeting_gx512.mp4` and `ds_candle_greeting_gx512.png` show the completed dialogue.
The matching original reference is `psx_candle_room_reveal.mp4` and
`psx_candle_room_revealed.png`, with state `ds_port_room_revealed`. The DS run keeps zero drops.
These compare sequence milestones, not identical timing or actor coordinates: DS animation
runs slowly in wall-clock captures, and the manually aligned interaction position differs slightly.
Candle/background placement agrees; character shading and the dark-room rectangular blend
artifact remain differences. Name entry uses untouched text rather than editing the game’s name.

## First battle exploration (2026-10-12)
The original PSX run was advanced from a verified New Game through candle lighting,
name confirmation and the Tantalus dialogue into battle 336 against masked Baku.
Reference states: `ds_port_tantalus_greeting`, `ds_port_baku_closeup`,
`ds_port_first_battle`, and `ds_port_first_battle_menu`. Animation references are
`build/validation/psx_baku_entrance.mp4` and `psx_first_battle_attack.mp4`.
The matching DS entrance captures exposed texture allocation failures and a cached
battle-overlay relocation failure, rather than an attract-movie transition.

Cached overlay prefixes restored by `func_80012900` now call `psx_overlay_patch`
after either the RAM copy or `StoreImage`; sectors not in that prefix are still
patched by the CD layer. See `trace_battle_loader.log` for the pointer watchpoint.
Battle initialization also submits an OT and packets inside one native stack
buffer. `port_draw_ot_buffer` receives that buffer's exact bounds at the counted D0940
call site. Links within those bounds use the head's native address window; other
links resolve into the PSX arena. Plain `DrawOTag` requires arena links. This
avoids an intermittent ASLR collision where a native link's low 24 bits also
address arena RAM. The regression probe forces this collision deterministically;
it also covers native-to-arena chains.

Nine battle scalar helpers, the background emitter/projector, the skeletal pose
builder and the eight-format model packet emitter have native implementations.
Three initialization reads/writes through NULL are patched at counted sites; their
original PSX values and the limits of the low-RAM shadow are documented in F4.
These changes advance execution into battle drawing; combat is still being traced.

Validation commands:
```
tools/docker.sh make test-gpu test-gx test-battle
tools/docker.sh make pc-gx
```
`test-gpu` exercises real 32-bit native/arena OT chains under ASan/UBSan.
`test-battle` compares 2700 scalar-helper cases against the original instructions;
160 background cases, 100 pose cases, 160 mesh packet cases, 1000 orientation/
interpolation cases, 400 particle-motion cases and 200 effect rectangle/sprite
cases compare CPU logic
against those instructions using shared deterministic GTE/rotation models.
The latter tests validate packing, culling, sorting, hierarchy and register paths;
they do not independently validate GTE precision, trigonometry or DS rasterisation.
Original instruction bytes are read from the user's generated decomp assembly at
runtime, rather than embedded in tests.

`build/tests/gx_host.elf` runs the real game through the production GX translator
with a contiguous 512 KiB texture allocator model. It prints texture versus geometry
budget failures and allocation pressure. It does not rasterise or model palette
exhaustion, physical bank remapping or DS timing. The enclosing-region cache avoids
duplicate uploads for smaller faces, including unchanged rows in dirty parents;
subregion reuse never marks other dirty parent pixels clean. Actual DS captures
remain necessary. Bottom-screen diagnostics report separate texture/budget counters.

The native effect path now includes E3EEC frame orchestration, five orientation/
interpolation helpers, both DFE4C/DFE54 particle integration entries, and
D4478/D40CC effect rectangle/sprite emitters. Native code uses its platform stack;
E3EEC's scratchpad-stack switches and unused saved-SP bookkeeping are omitted.
A counted E4A3C draft patch reconciles the MIPS stack-home struct with its native
four-argument callers. Battle help-text measurement uses a native ABI adapter
for the explicit MIPS hidden-return call (F5/F6 in `decomp-requests.md`).

`python3 tools/battle_smoke.py` drives a reproducible PC New Game through the
opening and Baku transition; pass `build/tests/gx_host.elf` for allocator tracing.
The script sends inputs at game frame counts, not wall-clock times. It is a
runtime smoke probe and cannot replace visual or interactive combat validation.

Fresh-build room checks: `psx_first_room_latest.png` agrees with the DS shot
`ds_interpolation_run.png` on the dark background and character spotlight.
`ds_battle_build_candle_motion.mp4` actually captures idle motion before the candle
approach, despite its filename; its contact sheet must not be described as candle
lighting. `psx_candle_choice_motion_latest.mp4` captures the PSX reference after
confirming the candle choice. DS wall-clock clips and PSX emulated-time clips have
different time bases; compare animation poses/transitions rather than durations.

The PC path gets through battle execution to mode 8 after the text ABI adapter.
That exposed a stale `func_801EF284` dispatcher name for overlay 65's existing
`battleEnd_main`; a counted call patch plus an explicit cross-overlay pin uses
the current name. `tools/pcdebug.sh` now retains `[frame ...]` status lines instead
of dropping all bracketed output, so progression can be reviewed in logs.
The DS loaded image is identified separately from the on-disk ROM when builds
continue during a run; captures from an older loaded image do not validate later
battle helpers. The software-display build also compiles successfully.

Latest full PC smoke: `pc_battle_end_progress.log` completes 60,000 frames normally.
Frames 24,000–26,000 run mode 2; frame 28,000 returns to field mode 1 with overlay
65 loaded. `pc_battle_frame_030000.png` and `pc_battle_frame_035000.png` show the
Tantalus briefing with Baku unmasked. This establishes story execution through
that encounter, not battle graphics completion: the 24,000–26,000 software dumps
are mostly black with a thin diagonal textured wedge (25,000 has the "Oww!" text).
They do not match `psx_battle_menu_latest.png`. A first-background transform probe
(`pc_battle_projection.log`) preserves vertex data and live GTE controls for the
next comparison. Real GTE/packet/viewport tracing is still required.
The longer GX host path also exposes renewed texture failures and geometry-budget
failures after the opening. Zero drops in the candle scene must not be generalized
to battle or later multi-character fields.

Follow-up visual issue (user priority after battle): rectangular/blocky artifacts
around Zidane are visible in the opening room, especially during expansion of the
candle light (`ds_current_reveal_progress.png`). Compare the moving character and
light edge against PSX clips after fixing the mostly black battle output. This
is a recorded rendering defect, not an expected result of screen scaling.

The final longer GX allocator run (`gx_battle_end_progress.log`) completes 60,000
frames and reaches the briefing, but reports 15,608 texture-allocation failures
and 75,312 geometry-budget failures cumulatively. These are the contiguous host
allocator model's counts, not measured DS hardware drops. The 60-second DS clip
`ds_latest_candle_reveal.mp4` records the growing pool of light and the rectangular
patches around Zidane; its contact sheet is `ds_latest_candle_reveal_contact.png`.

After the explicit native-OT bounds fix, `final_battle_scoped_checks.log` passes
DS/PC builds plus GPU, GX and all 4,720 battle equivalence cases.
`pc_battle_scoped_smoke.log` independently reruns to 30,000 frames, crosses mode 2
and returns to mode 1 at 28,000 without a fatal stub or stack overwrite. The
upstream decomp checkout remains unchanged.

### Battle camera and geometry shadow follow-up

The mostly black battle was caused by the port's GTE OP implementation: it stored
IR1 before calculating IR2 and IR3, which must all use the original input vector.
The camera consequently had two nearly antiparallel axes. Snapshotting the three
inputs fixes the camera basis. `pc_gte_cross_product_smoke.log` reaches the briefing
normally; `pc_battle_op_fixed_025000.png` and `pc_battle_op_fixed_026000.png` now show
the room, all four party members and masked Baku. DS battle validation is pending.

`make test-gte` checks the production GTE core with independent golden cases under
ASan/UBSan. `tools/test_gte_psx.py` additionally compares 32 OP vectors, including
shift, limit and saturation cases, with live PCSX Redux registers. It saves and
restores the emulator state around synthetic instructions; all cases pass.

The rectangular candle artifacts have a renderer fix under DS validation: model
polygons now rasterise into the CPU compositing shadow, preserving their actual
coverage and colours instead of invalidating a bounding rectangle. Subsequent
subtractive light sprites can therefore compose over the model. The software
rasteriser clamps interpolated channels before packing, preventing a negative
rounding result from causing undefined shifts. GX regression checks pass.

Consecutive opaque textured rectangles merge only when screen positions, UVs,
draw state and colours form one continuous rectangle. Pending rectangles retain
their draw environment and flush before geometry, frame submission and VRAM
writes. A regression verifies that a queued sprite samples its original texture
before a subsequent upload changes it. An ARM/O3/TCM experiment produced a black
field on DS, including with TCM and rectangle merging individually disabled.
Returning the GTE and rasteriser to Thumb/O2 restores Zidane and the opening
dialogue. The ARM flags are removed; no speed improvement is claimed. The compiled
ARM946 GTE independently passes the 32 PSX OP vectors, so that check alone does
not explain the regression. Signed shifts of negative GTE intermediates have
also been replaced with multiplication to keep that arithmetic defined.

`gx_merge_shadow_progress.log` completes 60,000 frames and reaches the briefing,
but reports 454,121 texture failures and 73,907 budget failures. The corrected
camera now exposes substantially more battle geometry; these numbers are not a
direct performance comparison with the old broken camera. A further lossless
texture optimisation stores composited tiles with at most sixteen exact colours
as four-bit indexed textures, sharing identical palettes. More colourful tiles
retain direct colour. Tests decode the result to verify every pixel, including
opaque black, transparent padding and the seventeen-colour fallback. Its full
allocator run and DS validation remain in progress.


### DS image-quality rollback (2026-10-12)

The geometry-shadow replay and indexed-composite experiments regress the actual
DS opening: the growing candle light acquires a stepped edge even before upload
failures, and later texture pressure loses parts of the scene. Host pixel checks
are insufficient to approve those changes. The default renderer again invalidates
polygon bounds in the sprite shadow, uploads composited tiles as direct RGB555,
and submits rectangles individually. It uses the original containing-region
preference and 64 KiB palette bank. These experiments require
`GXEXPERIMENTAL=1`; they must pass PSX screenshot and animation comparisons on DS
before becoming defaults. The GTE cross-product and defined-arithmetic fixes remain.
The older Zidane bounding-box artifact and texture-pressure issues remain open.


### Texture-pressure investigation (2026-10-12, continuing)

The first direct-colour failure in the opening-to-battle allocator probe pins
63 regions (446,464 bytes), but those regions contain only three distinct images
(16,384 bytes). Direct images now share exact converted pixels across source
regions, while binding retains each request's UV origin. Indexed page lookups
retain the previous array-order preference rather than changing enclosing-region
selection. Host tests cover shared images at different UV origins and a later
source write without overwriting an earlier submitted texture. The corresponding
DS build retains the round light pool; the old character bounding-box artifact
remains visible and its candle animation is being captured.

That sharing alone completes the 60,000-frame probe with 377,605 texture failures
and 73,982 geometry-budget failures. A frame-wide primitive collector is now
behind `GXPLAN=1`: it retains drawing state/framebuffer origin, chooses enclosing
regions before upload, and flushes before writes that intersect queued texture pages or CLUTs. Its current probe completes
60,000 frames with 310,869 texture failures and 78,450 budget failures. This is
still inadequate and has not been approved as the default DS renderer. The native
software path and DS/PC builds still pass; passing host allocator checks does not
establish image quality or emulator timing.


Indexed source-pixel sharing is additionally behind `GXSHARE=1`: immutable
pixel images have separate palette handles, and binding assigns the logical
palette to the shared GPU image. Tests draw the same indices through red and
blue CLUTs and release each handle while retaining the other image reference.
This does not enable the earlier indexed-composite or geometry-shadow experiments.
Direct framebuffer regions are excluded from atlas planning because their extra
padding increases memory pressure. Zero-area faces are rejected before upload,
matching the software PSX rasteriser. The allocator model now uses the default
64 KiB palette bank; earlier 96 KiB runs must not be treated as that configuration.


`gx_indexed_plan_progress.log` (before tighter UV origins) completes the same
60,000-frame path with 294,797 texture failures and 76,069 budget failures. The
64 KiB palette model reports no palette failures. This remains far from the goal.
A further `GXPLAN=1` change reduces padding by using exact UV minima (even minima
for four-bit packed pixels), while retaining 16-pixel minimum allocation sizes.
Dirty-generation sampling now covers partial eight-texel units; brute-force
checks include unaligned UV origins. This variant is awaiting its allocator and
DS comparisons. The default ROM is rebuilt without experimental flags after each
preserved diagnostic ROM.

The tighter-origin probe regressed badly and is no longer selected by `GXPLAN`.
At its first failed source upload (submission 9066), 539 cache entries were pinned,
192,256 bytes of pixel RAM were occupied, and 332,032 bytes were free. The largest
contiguous hole was only 19,712 bytes: an optional 256x128 indexed atlas needed
32,768 bytes for a face whose own UV region fits in 32x16. Atlas upload failure
now disables that atlas for the batch and retries the individual face region;
previously it dropped the face outright. A forced-allocation-failure fixture
checks the fallback, texture origin, and absence of a reported drop. This remains
an experimental planner change until DS visual validation.

The allocator probe's texture-name table now has 16,384 slots rather than 4,096;
libnds allocates texture names dynamically, so the former artificial limit could
reject allocations prematurely. Pixel RAM and palette RAM remain 512 KiB/64 KiB.
The production logical cache remains at 2,048 entries; enlarging it did not fix
the observed fragmentation failure.

Ten ARM9 PC samples from matched melonDS savestates landed in sprite sampling /
compositing (four samples), texture comparisons (three), libc memcmp (two), and
memset (one). These are a small sample, not a frame-rate measurement. The renderer
now compares aligned pixel data in words, hashes aligned words, and shades opaque
indexed sprites once per palette entry before sampling. ASan/UBSan fixtures cover
aligned/unaligned comparisons and independently computed modulation, transparent
texels, and opaque black in both four-bit and eight-bit palettes. DS/PC default
builds pass; actual DS performance and later-scene correctness still need checking.

The candle confirmation was captured from a saved choice screen, with capture
started before Cross, in `ds_indexed_plan_candle_confirm_to_reveal.mp4` and
`psx_candle_confirm_to_reveal_reference.mp4`. Contact sheets confirm rectangular
lighting artifacts around the DS character versus the PSX's smooth reveal.
Earlier clips labelled growth/expansion started after expansion or at the idle
choice and should not be used to assess the candle action.

Dirty texture blocks are now 64 words by eight rows, with separate 64-word
per-row CLUT generations. Tests prove writes at source row 224 do not invalidate
sampled rows 192–207, and that a write to CLUT row 301 leaves a palette at row 300
valid. Wrapped writes and brute-force generation checks still pass. The matched
DS diagnostic ROM/ELF is `ff9ds_cpu_precise`, booted fresh; old states would restore
old executable code. Its first dark-room dialogue reports zero dropped primitives.
The default ROM is built with `GXPLAN`, `GXSHARE`, and `GXMERGE` unset.

`GXMERGE=1` now enables only adjacent opaque textured rectangle merging, without
`GXEXPERIMENTAL`'s generated indexed composites or geometry shadow replay. Merging
retains the buffered primitive's framebuffer origin. A third ASan/UBSan build tests
this isolated configuration; host pressure and real DS image comparisons remain
required before considering it for the default.

The workstation's melonDS JIT was disabled. It is now enabled for the validation
run; the previous configuration is saved as `build/validation/melonDS_before_jit.toml`.
This does not establish target hardware performance. Only one melonDS instance is
kept open, and matched ROM/ELF/savestate sets are preserved before replacing it.

### DS battle validation, sound pointer crash, and UI insertion

The matched GX DS ROM `ff9ds_cpu_precise` verifies the GTE OP camera repair on
DS, with the room and all five characters visible. Large missing-texture patches
remain in battle. A real DS run then stopped with `SystemError('S', 310)`: sound
panning `func_800E976C` interpreted a native stack SVECTOR as a unit index because
the original PSX pointer-bit test does not recognise ARM stack addresses. A
counted patch distinguishes small IDs from native pointers using `PSX_RAM_SIZE`.
`tools/test_battle_pan.py` checks all 18 IDs, the -1 sentinel, and native static,
stack and DS-address pointers with the actual patched function and software GTE.
`ff9ds_battle_pan_fixed` passes the former crash during DS battle actions.

The DS software comparison path shows an intact battle room and removes the
rectangular candle-light patches seen in GX. Captures include
`ds_software_battle_entry.mp4`, `ds_software_battle_menu_compare.png`, and
`psx_first_battle_for_software_comparison.mp4`. These comparisons also exposed
missing battle UI in both port backends. `ContinueDraw` was empty, discarding the
VBlank UI ordering table even though `BreakDraw` correctly returned zero for an
idle synchronous renderer. It now executes the inserted list. `GetDrawEnv2`
snapshots the current GP0 registers, as the original PsyQ assembly does, so the
UI list can restore clipping, offset, texture/window and mask state afterward.
The GPU fixture checks idle insertion, a following continuation, and all six
register snapshot words. `pc_battle_ui_25000.png` shows command/status panels;
the 30,000-frame PC opening run returns to the briefing without a fatal.

Use `tools/save_ds_state.py` for slot-8 captures: saving via XTest is asynchronous.
Immediately copying the slot can preserve an older state. The helper waits for a
fresh complete write and reports the game frame and guest VBlank count from the
matching ELF. The former `ds_cpu_precise_first_battle.ml8` is actually a stale
frame-3396 name-screen state and must not be used as battle evidence. Guest
VBlank deltas permit FPS measurements despite accelerated emulator wall time.

Standalone GX rectangle merging did not reduce the 60,000-frame host pressure
probe: 272,004 texture failures and 76,069 budget failures with or without it.
Cross-CLUT planning regressed allocation pressure and was reverted. Both remain
outside the default. Hardware rendering is still incomplete.

`ff9ds_software_ui` is a fresh DS boot with the UI insertion and flat-colour
interpolation optimisation (before palette pre-shading). Its command/status
panels match the PSX layout in `ds_software_ui_battle_verified.png`. The actual
candle choice/action/reveal is captured in `ds_software_ui_candle_action.mp4`;
its contact sheet shows the restored light without GX's rectangles.
First-battle guest-time measurement from frame 6095 to 7644: 1549 game frames
over 13691 emulated VBlanks, approximately 6.77 FPS. This remains a serious
performance issue, despite unlimited emulator pacing and JIT.

Software rendering is now the default; `SOFTDISPLAY=0` explicitly selects GX.
The flag now compares against 1, so 0 no longer accidentally enables software.
The software pixel loop skips RGB gradients for flat polygons and pre-shades
large flat indexed primitives once per CLUT entry. Original texels are retained
for transparent-zero and STP decisions, and caching is disabled when writes
could modify the CLUT. Texture-index reads remain live. `make test-raster`
compares full VRAM against the generic path for 4096 random mixed primitives,
plus wrapped palette-feedback, opaque-black and shadow mask/known-pixel cases.
ASan/UBSan and all three GX fixtures pass. A host O2 flat-indexed-quad benchmark
measured 0.577s generic versus 0.421s optimised (1.37x); it is not a DS FPS result.
Interpolant stepping now uses defined unsigned 32-bit addition to retain register
wrapping outside thin triangles without C signed-overflow UB.

`make raster-ds` builds a disc-free ARM9 timing ROM using the production renderer
and generic oracle, comparing Thumb and ARM implementations with identical
VRAM and checking resulting pixels. Guest timer counts, not wall time, are used.

The disc-free ARM9 timing probe also checks solid blended polygons and rectangle
fills. Pixel outputs match in seven cases. ARM plus instruction-TCM placement
reduces guest timer counts in all seven: flat indexed quad 51.31M -> 27.82M,
solid Gouraud subtract 37.66M -> 27.20M, solid rectangle 21.29M -> 0.90M
(for 64 96x96 draws). Captures: `ds_raster_timing_extended.png`,
`ds_raster_timing_itcm.png`; machine-readable counts are in
`raster_arm9_timing_summary.json`. These are isolated raster measurements, not
whole-game FPS. Forcing texture samples inline helps rectangles but has mixed
polygon results outside TCM; the TCM version improves the tested cases.

Only DS `port/psx/softgpu.c` is now compiled in ARM mode at O2, with triangle,
line and rectangle raster loops in `.itcm` (total game ITCM 11544 bytes). GTE
code and its flags are unchanged. The former ARM/O3/GTE experiment's failed
room test is not reused as validation. `ff9ds_software_fast` is a fresh matched
ROM/ELF boot for this configuration, with automation ending before battle inputs.

The fresh optimised DS run shows an intact battle and UI, then manually completes
the encounter and returns to Baku's briefing. `ds_software_fast_battle_verified.png`
and `ds_software_fast_after_manual_inputs.png` preserve those states; animations
are in `ds_software_fast_battle_entry.mp4` and `ds_software_fast_manual_attack.mp4`.
An idle battle interval measures 3125 game frames / 21193 guest VBlanks = 8.82 FPS
(frame 5831 -> 8956), versus the pre-palette/TCM UI build's 6.77 FPS interval.
Intervals differ in exact enemy animation phase, so this is an approximate
whole-scene comparison, not deterministic per-frame timing. Performance is still
far below the target. DS/PC default and explicit GX builds pass.

The loaded PSX first-battle reference state later proved stuck at VSync+0x24,
continually trying to read root counter 1 twice with equal values. GPU VBlanks
advance but game pad globals remain zero under Cross/Circle holds. Thus the latest
`psx_battle_confirm_attack_reference.mp4` is an idle clip, not attack evidence.
Static battle comparisons remain useful, but animation requires a fresh reference
boot with verified input and game progress. The reference is being cold-restarted.

### Fresh PSX reference and post-briefing execution failure (2026-10-12)

DuckStation keyboard bindings were empty; applying automatic keyboard mapping gave Cross K, Circle L, Start Enter, and arrow directions. New Game was explicitly selected and captured (`duck_newgame_cursor_verified.png`), followed by the storm opening and playable Zidane dark room. `duck_candle_action_reference.mp4` captures confirmation of Light the candle, the character motion, room reveal, and name screen; its contact sheet confirms animated progress. Window capture is wall-clock time, not lockstep game-frame timing. This replaces the stalled Redux attack attempt as a working reference for future animation checks.

The optimized software DS route completed the battle and briefing quiz (Princess Garnet selected), then stopped at a black transition. Two matched savestates report game frame 52369 unchanged while guest VBlanks increased 469864 to 479065. The ARM9 state has PC 0xFFFFFFFE and CPSR 0x024900F8; CP15 control is 0x78 instead of the prior running state’s 0x5307D. The cause is unresolved. An interpreter replay from the matching pre-briefing state is underway; do not count this route as passing past the briefing.

GUI key streams must run sequentially: XTest keys target the currently focused window, so manipulating a second emulator while a key script is running can steal its input focus. Passive window recording may run concurrently.

The interpreter replay reproduced a data abort at the full-screen TIM routine `func_800B4A5C` after selecting Princess Garnet. The correctly routed PC smoke test faults at WIP line 131 as well. Both CPU-side uses of the stored OT[8] link now pass through `PSX_OT_PTR`, restoring the arena high byte without adding the PSX arena base a second time. Original MIPS confirms that following the stored link is intentional. The GPU fixture checks the linked-bucket insertions and packet order under ASan/UBSan. Fresh DS and PC route validation is pending; old savestates contain the old executable and cannot validate this fix.

The first fix lets the PC pass the full-screen TIM loop, exposing a second crash at evaluator opcode 0x61 (distance to an object). Its popped 26-bit object pointer 0x02077F04 becomes 0xFE077F04 after signed expansion; the previous identity `SCRIPT_PTR_RESTORE` hook failed to remove that expansion. The hook now delegates to the same 26-bit mask used for event opcodes 0x70/0x71. Native pointers remain within the 0x02xxxxxx arena. Route validation continues with both fixes.

With both pointer fixes, `tools/battle_smoke.py --briefing --frames 60000 --dump 60000` finishes normally and captures Vivi in Alexandria (`pc_postbriefing_both_fixed_60000.png`). This newly added quiz route selects Princess Garnet; the prior smoke route loops on Queen Brahne and does not cover the TIM transition. The fresh DS build `ff9ds_script_pointer_fixed` is undergoing the same route from boot.

The fresh DS validation includes manual early candle/name inputs, so its battle begins earlier than the PC smoke schedule. Consequently the scheduled Start@20000 paused the battle. A matched savestate was copied with only the port’s scripted-pad count reset to zero (`ds_both_fixed_manual_battle_no_script.ml8`, JSON records the one-word change), then restored with the same ROM/ELF. Start unpaused it, and manual controls resume encounter validation. No game state, program bytes, or progress were changed in that diagnostic copy.

The fresh both-fixes DS run now manually completes the battle and reaches the briefing. `ds_both_fixed_manual_attacks.mp4` and its contact sheet show actual player attacks, flashes and enemy movement; this replaces the earlier idle ability-menu clip as attack evidence. The PSX combat clip likewise shows real attacks, but the Alexandria arrival clip begins at the already-reached “Are you awright?” dialogue and only records idle character motion, not the FMV-to-field transition. PC Alexandria exploration to 85000 frames exits normally and captures Vivi in the square; fresh DS quiz-transition validation remains pending.

The fresh `ff9ds_script_pointer_fixed` DS run selects Princess Garnet (`ds_both_fixed_princess_selected.png`) and passes the formerly crashing transition to Vivi’s “Are you awright?” scene (`ds_both_fixed_transition_live.png`). The matched state reports game frame 57160 / guest VBlank 341228. Both pointer fixes are now verified on DS as well as PC. `ds_both_fixed_postquiz_transition.mp4` records the quiz confirmation and transition; no FMV playback is claimed, since the movie override is still headless.

The rasterizer now skips UV setup for untextured polygons and gives untextured Gouraud lighting overlays a loop that only steps RGB. The first layout regressed textured ARM9 cases by 2–5%; putting the flat branch first recovers most of that cost. The reordered disc-free DS probe matches all seven full-VRAM outputs and measures 24.45M ticks for the solid Gouraud subtract case, versus 27.20M previously (10.1% reduction). Flat indexed 27.99M vs 27.82M, textured Gouraud 39.50M vs 39.45M, subtract 31.04M vs 31.28M. Capture/JSON: `ds_raster_uv_reordered_timing.png`, `raster_arm9_uv_reordered_summary.json`. Host ASan/UBSan differential passes. This is isolated raster timing, not a new whole-game FPS measurement.

### Font coverage during DS display scaling (2026-10-13)

Nearest-neighbour 320-to-256 scaling discards thin font strokes. A full-frame area-coverage prototype matches an independent scalar test, including wrapped VRAM, RGB channels and clipping. ARM/ITCM grouping and exact small reciprocal division reduce its 320x224 cost from 1.24M to 0.735M guest ticks/frame, still too expensive. The production candidate filters only cells touched by decoded proportional-font sprites (E100061E, height 13) and small-font sprites (E100061F, 8x8), identified from main.c. It retains font regions across extra VSyncs without drawing. Scene pixels keep nearest sampling, while glyph-border coverage preserves their strokes. The bitmap is pre-scaled and the DS affine unit runs at 1:1; display-size changes clear old pixels and update centering.

Host ASan/UBSan tests check known strokes, independent channel sums, wrapping, cropped borders, selective-vs-full coverage, empty selection and the decoder’s font-page/size/clipping decisions. The disc-free ARM9 timing probe measures 172610 guest ticks/frame without glyphs and 458846 with 100 glyphs, versus 734791 for full coverage. This adds CPU work and is not a whole-game performance result. `ff9ds_text_coverage` is a fresh ROM/ELF with the exact PC briefing input schedule; game appearance and whole-scene timing validation remain pending. Prior saved states must not be loaded into this executable.

The fresh PC build with font-region notifications and the reordered raster loop completes the Princess Garnet route to 60000 frames and exits normally (`pc_text_coverage_60000.log`, `.png`). The new DS image is fresh-booted, with the original PC input schedule and no manual advance inputs. Runtime ITCM use is 17656 bytes. GPU ordering and all three GX texture/cache fixtures pass.

The first fresh-run screenshots do **not** validate dialogue filtering: a runtime snapshot during the instruction text shows zero selected font rectangles. The initial page predicate covered the main.c UI page 1E but missed script_text.c’s 13-high dialogue page 1F (`func_800515EC`). The matcher now includes both proportional-font pages, with a regression test for page 1F at height 13. A fresh `ff9ds_text_both_pages` image replaces that validation attempt. The apparent initial readability improvement was subjective and must not be treated as evidence that the filter had run.

The broader page predicate does not resolve the live handoff: `ds_both_font_pages_first_text.ml8` still has zero selected rectangles and zero retained filter-mask bits. Replaying the old instruction-text OT through the production decoder finds 43 font sprites on page 1E, so the missing-page hypothesis alone was incorrect. A targeted fresh probe records decoder font candidates, matcher/clip acceptance and DS glyph-call/presentation counters. The candidate is not yet visually verified or accepted as a readability fix.

The targeted first-dialogue probe (`ds_font_handoff_first_dialogue_probe.json`) confirms 7823 13-high candidates, 9799 total font matches and 8818 accepted glyph calls, with 18 glyphs at the last presentation. Thus the decoder/hook did run; the initially inspected zero live count was from the next in-progress draw. The empty retained mask points to buffer timing: text drawn into the next PSX framebuffer replaces the metadata for the currently displayed buffer before `PutDispEnv` switches it. The candidate now keeps the previous draw’s rectangles alongside current ones and chooses coverage by VRAM/display coordinates. A host fixture checks that next-buffer glyphs do not replace the displayed buffer’s thin-stroke coverage. Fresh `ff9ds_font_buffer_history` runtime validation remains pending. The added page 1F coverage remains correct for script_text.c, but missing that page was not sufficient to explain the old runtime evidence.

The fresh buffer-history run now confirms active coverage: `ds_font_history_first_dialogue_probe.json` reports previous_count 18 and 1144 retained filter-mask cells at game frame 4869 / guest VBlank 35461. The old probe had zero masked cells. `ds_font_history_first_dialogue.png` and `dark_dialogue_font_history_comparison.png` compare the actual filtered DS dialogue with the former nearest-font DS capture and DuckStation dark-room reference at DS size. The screenshot comparison uses window crops/resizing, not a pixel-exact PSX framebuffer extraction. Runtime animation and battle-performance checks remain pending.

The working PSX reference is saved in DuckStation game slot 1 at Vivi’s “Are you awright?” scene (`SLUS-01251_1.sav`, copied to ignored `build/validation/duck_vivi_arrival_reference.sav`; save confirmation in `duck_vivi_reference_saved.png`). DuckStation is closed while fresh DS validation continues, so only one emulator remains running. Relaunch the same DuckStation AppImage/disc and load game slot 1 for later Alexandria comparisons.

### Horizontal triangle edge coverage

The native first-dialogue VRAM snapshot contains 272 non-black pixels on display row 0, with rows 1–3 black; the DuckStation dark-room screenshot does not show that stale line. The rasterizer’s horizontal top-left test used B<0, excluding a top edge directed left-to-right in screen coordinates. Changing the horizontal case to B>0 covers the top row. A fixture compares a screen-aligned quad with a rectangle, including all four blend modes and opaque output; the 4096-primitive ASan/UBSan differential passes. Fresh PC at frame 4500 (`pc_top_edge_dark_4500.png`) now has zero non-black pixels on the top row. Fresh DS validation of this change is pending; the currently running buffer-history ROM predates it.

`tools/xkey.py` now sends the window manager an EWMH activation request before raw focus. This restores Qt savestate shortcuts after closing another emulator; the previous failed slot-8 save was retried successfully, with frame 22044 / guest VBlank 200996 recorded in `ds_font_history_name_route_resume.ml8`.

For manual candle animation capture, the matched buffer-history state at frame 4869 was copied with only `n_presses` set to zero (`ds_font_history_candle_manual.ml8` plus JSON). The same ROM was restored, dialogue advanced, Zidane walked to the candle and the actual “Light the candle / Cancel” prompt was captured (`ds_font_history_candle_question.png`). The untouched automated route is preserved at frame 22044 in `ds_font_history_name_route_resume.ml8`. This running ROM still predates the horizontal-edge fix; `ff9ds_top_edges_text.nds/.elf` is built separately for fresh validation.

### Text shimmer / visible intermediate bitmap (user report)

The user reports frame-to-frame movement on the candle prompt and naming screen, rather than the first dark-room dialogue. Consecutive naming frames from `ds_font_history_candle_light_reveal.mp4` show alternate crisp and softened glyph shapes (`name_consecutive_frames.png`). The scaler writes a nearest image and then filtered font pixels into the single bitmap being scanned out; its measured work outlasts VBlank. The candidate now uses two 256x256 Bmp16 buffers in VRAM A/B, renders into the hidden buffer, waits for VBlank and changes BG map base after both passes finish. Output stride is now 256. Software `plat_wait_frame` lets display_present perform that wait after rendering; GX retains its prior wait. Coverage/wrapping/border tests pass with the new stride. `ff9ds_double_bitmap` includes the top-edge fix and buffer-history font selection, but fresh DS shimmer/performance validation is pending. The fast text-only fresh-boot input schedule is saved separately and ends at frame 5200; it does not claim opening-to-battle coverage.

The fresh DS top-edge build now also has zero non-black pixels on display row 0 (`ds_top_edge_fresh_progress.ml8`, frame 11287, `ds_top_edge_probe.json`), confirming removal of the stale line on DS as well as PC. The saved source state is matched to `ff9ds_top_edges_text.elf`. The double-bitmap text probe is now fresh-booted for candle/name consecutive-frame validation; only melonDS is running. The user enlarged its window to 1303x1203, so earlier fixed screenshot/video crop coordinates must not be reused.

The older top-edge emulator failed to close via Alt+F4; launching the double-buffer probe without verifying exit briefly left two melonDS instances. After the user pointed it out, SIGTERM did not terminate PID 2204242; SIGKILL closed that specifically identified older ROM process. Verification shows only PID 2214624 with `ff9ds_double_bitmap_text_probe.nds` remains, window 0x6a00007 (716x875). Subsequent ROM switches must verify the old process is terminal before launching another instance. Fresh double-buffer probe reaches frame 4247 / guest VBlank 30267 without a crash; text stability still needs consecutive-frame comparison.

Fresh double-buffer naming-screen verification: `ds_double_bitmap_name_consecutive.mp4` records a static alphabet grid. Comparing 330 consecutive frames over 11 seconds in native-screen ROI (100,84,136,32), with a >24 RGB-change threshold to exclude minor lossy-codec noise, the old capture has 38 transitions with >10 changed pixels (max 369); the double-buffer capture has zero changed pixels above threshold. `font_jitter_before_after.json`, `name_consecutive_frames.png`, `name_double_buffer_consecutive_frames.png` preserve measurements and frame grids. This confirms removal of the reported naming-grid shimmer in that interval; candle-prompt and battle/performance validation still need completion. Default software and explicit GX builds pass; build/ff9ds.nds is restored to the manual software configuration (no scripted inputs).

Candle-prompt verification is also positive: `ds_double_bitmap_candle_prompt_consecutive.mp4` contains the actual “Light the candle / Cancel” prompt after manual replay from the matching frame-4247 state with only scripted-pad count disabled. Its text ROI (112,52,74,20) has zero >24 RGB pixel changes across 330 consecutive frames / 11 seconds (`candle_prompt_jitter_double_buffer.json`, `candle_prompt_double_buffer_frames.png`). The earlier 60-second file named `ds_double_bitmap_candle_name_stability.mp4` is entirely the naming screen; its contact sheet confirms it is not candle-action evidence. This fixes the reported shimmer in both tested static UI regions, not a claim that all graphics/performance issues are solved.

`ds_double_bitmap_candle_reveal.mp4` and its contact sheet now capture the real new-build candle sequence, from the selected Light prompt through hand motion, room reveal, fade and name screen. The actual sidebar Confirm action is selected in `ds_double_bitmap_name_confirm_selected.png`; the following manual dialogue route proceeds toward the first battle. These captures use the fresh double-buffer/top-edge ROM and therefore supersede pre-fix animation footage for this display configuration.

### Idle battle crash after Marcus KO

The new-build idle battle interval is invalid as an FPS measurement: both snapshots have game frame 29289 while guest VBlanks advance 247032 to 257055. The screen freezes on “Get some!” with Marcus knocked out. ARM9 is at PC 0 / abort mode, with no populated libnds exceptionRegisters. The first emulator abort logs point into ITCM sg_rect at 0100007C, followed by stack-related aborts and prepare_palette at 0209E42C. Comparing serialized physical ITCM with the matching ELF confirms code corruption at bytes 4,12–15,18–19 near sg_rect’s entry; IRQ-table/list changes later in ITCM are expected mutable data. Root cause remains unresolved. Do not attribute this to the display change without a reproducer or count this route as passing.

The bad PC generated a runaway 59-GiB emulator log and filled the host filesystem. The emulator was stopped and killed, the log’s first 256 KiB and last 16 KiB preserved, and repeated output removed; `melonds_double_bitmap_text_probe.truncated.json` records its original size. Free space is restored (~59 GiB). Future emulator runs use `tools/bounded_log.py`, retaining startup/latest output within ~1 MiB. The PC smoke harness now supports `--idle-after FRAME`; an enemy-attack/KO replay to 45000 frames with inputs ending at 26000 is underway. No emulator is currently running.

The PC idle replay completed 45000 frames without a fatal error (`pc_idle_ko_45000.log`), with party KO occurring, so it does not reproduce the native DS failure. A DS-only diagnostic build (`ITCMGUARD=1`) protects the first 16 KiB of linked ITCM with MPU region 7; DTCM retains region 6 access. Region 2 also makes the low 4-KiB ITCM alias read-only, to catch near-null packet writes. The diagnostic remains opt-in. `defaultExceptionHandler()` is now installed before game startup. The first guard build reached the dark-room/candle route normally; the alias-guard ROM is being booted fresh to trace the battle corruption. Each ROM switch verifies the previous melonDS process has exited. No old executable savestate is loaded into a new build.

The fresh alias-guard replay completed candle/name and reached battle. Its natural-run states at frames 11040 and 12677 show normal ARM9 execution, zero changes in the first 16 KiB of ITCM, and no captured exception. The interval measures 9.526 frames per emulated second at 60 guest VBlanks/sec (`itcm_alias_guard_idle_timing.json`); this remains a performance issue and is not a hardware wall-clock measurement. Two short idle battle clips and their contact sheet show enemy motion and Cinna's KO; the existing PSX combat recording supplies a visual reference. This is not a synchronized per-pixel or animation-timing match.

`ds_itcm_alias_guard_two_ko_natural.ml8` preserves the natural run at frame 24317 with Zidane and Cinna KO. A separate diagnostic copy changes only Battle.units[2].hp from 70 to 1 (`ds_itcm_alias_guard_marcus_one_hp.ml8.json`), to exercise Marcus KO sooner; it does not change executable code or set KO flags. Results from that copy must not be reported as an unmodified route pass. `tools/inspect_ds_state.py` reports saved CPU registers, exception registers, and ITCM differences against the matching ELF; it is a diagnostic tool, not a completion gate.

That controlled replay produces a captured exception after Marcus KO at frame 25469 (`ds_itcm_alias_guard_marcus_ko_probe.ml8`, `ds_itcm_alias_guard_after_marcus_ko.png`). The first 16 KiB of ITCM remains identical to the matching ELF. The stock handler itself then reports an undefined-instruction exception in getExceptionAddress, replacing its shared register snapshot; its displayed PC must not be attributed to the original game fault. Guard builds now install a minimal handler which saves the first 16 registers plus BIOS exception metadata into `port_itcm_first_fault` before printing anything, and avoids instruction decoding. `ff9ds_itcm_first_fault` is built and being replayed from fresh boot; the older crashed emulator was closed and its termination verified. The original low-pointer writer is still unresolved.


### Nullable texture-animation controls and systematic audit

The original DS idle crash is traced to `func_800BD5A0`, not the rasterizer: Marcus and Blank have `BtlUnit9C.unk50 == NULL`. Original disc MIPS at 800BD5E0/600/614/628 unconditionally stores at offsets 0x2c, 4, 0xc and 0x12. The writes at 4, 0xc and 0x12 match the damaged ITCM bytes exactly. The guarded native replay logs the original abort at ARM 02042088; a secondary BIOS prefetch abort explains why the ordinary exception display lost the game PC. Original disassembly is retained in `psx_marcus_ko_texanim_mips.txt`.

Counted patches now map dereferences of optional texture-animation data into the PSX low-RAM shadow, including KO, revival/status changes, battle scripts and summon helpers. Loader assignments and NULL tests retain their original meaning. The AST inventory finds 36 accesses across ten routines in this family, including a dereference embedded in assignment. `PSX_NZ` now evaluates its operand once, so a helper call is not repeated when non-null. `test-low-ram` checks the original KO store offsets, preservation of other bytes, unchanged live-pointer access and single evaluation under ASan/UBSan.

The PC controlled replay lowers Marcus and Blank to one HP at frame 26000, sees both absent-animation KO calls, and completes 65000 frames in the results screen without a fault (`pc_nullable_texanim_ko.log`). The first corrected DS ROM is freshly booted through candle/name/battle; its diagnostic copy lowers only those two HP values. At frame 23274 both are KO and battle keeps moving: the saved CPU state is normal, the first-fault marker is zero, and the first 16 KiB of ITCM matches its ELF (`ds_null_texanim_fix_after_marcus_ko.ml8`, `null_texanim_fix_after_ko_inspection.json`). The ROM includes the initial KO/status mappings; subsequently identified revival/summon/helper aliases are compiled in the normal build but still need fresh native validation. This is a controlled regression pass, not a complete game-route or performance pass.

`tools/audit_low_ram.py` uses Clang ASTs to inventory zero assignments/nullable returns and their propagation into member, indexed, cast and pointer-arithmetic dereferences. Reports stay in build/. The matched-source scan covers 24 files and 3859 bodies; 1229 routines contain conservative candidates in the initial report. One recovered argument-signature diagnostic in ovl_0c remains explicit in coverage. These counts do not mean that every candidate is a reachable null dereference: guarded and initialization-dependent accesses need control-flow and original-MIPS review. Indirect calls, unresolved integer/pointer flows, assembly/stubs, and header-only functions are not comprehensively covered. The native MPU guard remains an opt-in backstop; the full port goal remains open.


The audit also scanned 27 equivalent/draft C files (114 parsed bodies, eight routines with candidates). The known E4A3C stack-home ABI draft has a conflicting header signature and remains an explicit recovered frontend error; the port replaces that routine with native code. These reports therefore cover source inventory rather than proving complete executable coverage. The corrected controlled DS KO route advances into mode 8 after defeat with Masked Man's “Use a Potion to restore HP!” dialogue at frame 46965, first-fault marker zero and unchanged instruction region (`ds_null_texanim_fix_two_ko_followup.ml8`). This extends the passing regression beyond the immediate KO animation.


The controlled DS defeat route returns to the field, showing Baku's “You're lookin' a lot better!” dialogue (`ds_null_texanim_fix_after_defeat_dialogue.png`, `ds_null_texanim_fix_defeat_exit.mp4`, matched field-return state). The latest normal manual software ROM/ELF is preserved as `ff9ds_nullable_animation_family`; the full family and single-evaluation helper also pass the repeated 65000-frame PC KO regression (`pc_nullable_animation_family_ko.log`). Fresh DS validation of that expanded family, other audit candidates, graphics fidelity and performance remain outstanding.
