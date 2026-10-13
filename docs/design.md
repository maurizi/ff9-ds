# Port architecture

## 1. Starting point (ff9-decomp as of 2026-10-11)

| | state | what it means for the port |
|---|---|---|
| game C | 3821/3837 functions match (98.8% of bytes); 15 of the 20 binaries are complete | nearly all game logic compiles as-is |
| left as asm | 16 game functions (drafts exist in `docs/wip/`), ~150 hand-written asm routines, 542 PsyQ/BIOS functions | the port must supply C for every routine it calls. See §4 |
| names | about 2% of functions and 5% of globals have names | the port can't wait for names. It hooks at PsyQ/hardware boundaries, which are already named, and asks for names only where the dual-screen UI needs game semantics |
| binaries | the exe plus 19 overlays. Six overlays share 0x800A7000 and thirteen share 0x801EDF00 | overlay symbols collide; see §3.3 |
| data | `.data`/`.rodata` exist only as splat `.s` generated from the user's exe | the port generates its data from the user's exe at build time; see §3.4 |

Hardware coupling in game C, counted in `src/`: ~960 GTE macro uses (73 distinct macros), 335 `addPrim`, ~400
textured polygons/sprites set up by hand, 88 `LoadImage`, 34 `StoreImage`, 30 `MoveImage`, direct SPU register
pokes in the AKAO driver (`SPU_REG16`, `SPU_VOICE_REG`), scratchpad pointers (`0x1F800000`), and 33 distinct
hard-coded RAM addresses (`0x801EDF00`, `0x801FCB00`, ...).

## 2. Target hardware

**Default target: DSi mode** (DSi, 3DS/2DS via TWiLight Menu++ or nds-bootstrap, flashcarts that support DSi
mode): 134 MHz ARM9, 16 MB RAM, SD access from the ARM7. Original DS mode (67 MHz, 4 MB) is a stretch goal: see
§8 and `decisions.md` D1.

| resource | PS1 | DSi mode | notes |
|---|---|---|---|
| CPU | R3000A 33.9 MHz + GTE | ARM946E-S 134 MHz (+ ARM7 33 MHz), 8 KB D-cache, 16 KB I-cache | the GTE becomes software; the DS geometry engine can take over hot paths later |
| RAM | 2 MB | 16 MB | the whole game, all overlays, a 1 MB VRAM shadow and a PCM sample cache all fit |
| scratchpad | 1 KB | DTCM 16 KB, ITCM 32 KB | scratchpad → DTCM; the GTE core and the OT walker → ITCM |
| VRAM | 1 MB, framebuffers included | 656 KB in banks; the 3D engine writes the screen directly | 512 KB for the current GX path; planned effects reduce this budget; see §6 |
| GPU | ~ 90k–180k polys/s, 4 blend modes | 2048 polygons / 6144 vertices per frame, alpha blending only, depth buffer, fog, 4bpp/8bpp paletted textures in the PS1's BGR555 | per-frame budget and blending are the main risks; see §5.4 |
| sound | SPU: 24 ADPCM voices, ADSR, reverb, 512 KB | 16 channels (PCM8/PCM16/IMA-ADPCM; 14–15 noise), no ADSR or reverb, 2 capture units | software ADSR on the ARM7; see §7 |
| storage | CD, 4 discs | SD card | everything is on SD; disc swaps resolve instantly |
| display | 320×224/240 | 2 × 256×192, bottom one touch | see `dual-screen.md` |

## 3. Turning decomp C into an ARM binary

### 3.1 How the source is consumed (implemented in M0)
- `ff9-decomp` is a **git submodule** (`decomp/`). The port compiles `decomp/src/**/*.c` and
  `decomp/src_equiv/**/*.c` with `arm-none-eabi-gcc` 16 (BlocksDS) and never edits those files. Port-specific
  behaviour comes from these mechanisms, in order of preference:
  1. **`tools/portify.py`**, a mechanical copy of each file into `build/portified/`. It drops the MIPS rodata
     padding `__asm__` statements (any other inline asm fails the build). It wraps hardware addresses
     `0x1F80xxxx` in `PSX_HW()`, and PS1 RAM addresses `0x80xxxxxx` in `PSX_ADDR()` (§3.5). Exceptions go in
     `config/addr_plain.txt`; there are none yet. The 16 RAM literals not in pointer casts are listed per file in
     `build/portified/**/*.addr.txt`.
  2. **A forced-include prelude** (`port/include/port_prelude.h`). It turns `INCLUDE_ASM`/`INCLUDE_RODATA` into
     nothing and includes the file's own header (`ff9.h` or `ovl_XX.h`). Then `port_hw.h` `#undef`s and
     redefines every hardware macro those headers define, and the file's own `#include` becomes a no-op through
     its include guard:
     - the ~96 GTE macros, translated instruction by instruction into the software GTE's
       `mtc2`/`mfc2`/`ctc2`/`cfc2`/`cmd` (`port/psx/gte.c`), so register state persists between split macros
       the way it does on hardware;
     - `addPrimLen`, the scratchpad stack switch (a no-op), `SPU_VOICE_REG`;
     - `gte_ldrotkey`, which isn't GTE at all: it's a CPU animation-key decode, `port/game/anim_key.c`.
     Where an asm macro has a branch, the C follows the original *bytes* (they're `noreorder`, so delay slots
     always execute: e.g. `gte_stsvclamp`, checked at ovl_0a 0x1460C).
  3. **Port prototypes** (`port/include/port_decls.h`). Some calls come before any declaration, and gcc 2.8
     treated them as `int f()` when the definition is narrower; modern gcc rejects that. `tools/gen_decls.py`
     generates per-binary prototypes from gcc's own diagnostics into `port_decls_gen.h`: 30 so far. One call
     site passes 5 arguments to a 4-parameter function (`ovl_0a func_800C6C04`), which is handled by hand.
  4. **PsyQ replacement**: the game calls PsyQ by name (`LoadImage`, `DrawOTag`, `CdRead`, ...).
     `port/psx/` implements that API, so all 542 library routines go away without game edits.
  5. **Link-time overrides** for game functions that must behave differently on DS (the CD loader, the overlay
     loader, the field BG renderer, UI hooks): `objcopy --weaken-symbol` on the decomp object, so the port's
     definition wins.
  6. **Upstream decomp changes**, only for what 1–5 can't handle, and never changing PS1 codegen.
- Flags: `-std=gnu89 -fpermissive` (K&R-era C; gcc 16 defaults to C23, where `f()` means no arguments),
  `-fsigned-char` (MIPS `char` is signed, ARM's isn't), `-fno-strict-aliasing -fwrapv
  -fno-delete-null-pointer-checks -fno-builtin-memcpy`, `-O2 -mthumb`. Hot code moves to ARM/ITCM later.
- **Structure layout** is the same on both CPUs (little-endian, 32-bit, same natural alignment), so on-disc data
  can be read in place. `tools/layoutcheck.py` (to do) will compare `sizeof`/`offsetof` from DWARF for both
  toolchains, to catch bit-field differences.
- **Ordering tables** keep their 24-bit pointers (`P_TAG.addr:24`). DSi main RAM is exactly the 16 MB at
  `0x02000000`, so the OT walker rebuilds each pointer as `0x02000000 | addr24`.

### 3.2 Remaining asm (decomp side)
The port needs a C body for every non-library routine still in asm. A **functionally equivalent** body is enough.
These live in the decomp's `src_equiv/<binary>/` with the same names and headers, and the port compiles them
like `src/`. Until one exists, the link stubs it (§3.6). See `decomp-requests.md` R1.

### 3.3 Overlays: namespaces for code, slots for data
- Every overlay's code is linked into the one ARM9 binary. `tools/gen_link.py` writes an
  `objcopy --redefine-syms` map per binary. Each overlay function, whether defined in its objects or listed for
  it in the decomp's function index, becomes `oXX_<name>` (`ovl_0b` → `o0b_func_800A859C`). A reference to
  another binary's function is pointed at that binary's name.
- **When the target depends on the resident overlay**, the generator emits a trampoline instead of picking one.
  On PS1, main calling `0x800BDED4` means ovl_0a's function in the field and ovl_0b's in battle. The trampoline
  (`build/gen/tramp.s`) is a 4-instruction-per-candidate ARM veneer: it reads `psx_ovl_resident[slot]` and
  tail-jumps to that overlay's function, using only `ip`, so arguments arrive untouched. It currently covers 4
  references: `func_800A859C`, `func_800BDED4`, `func_800C1A8C`, and the menu table word `0x801F4748`.
  `config/xrefs.txt` can pin a reference to one overlay instead.
- **Overlay "loading"** (`psx_overlay_load(id)`) copies the overlay's data image into its PS1 slot in `psx_ram`
  and marks it resident, so its `.data` is reinitialised exactly as a PS1 reload would. The game's loader is
  overridden to call it for dir-0 code files (M1).

### 3.4 Data: the user's bytes, relocated (`build/gen/img_<bin>.s`)
- `make extract` runs the decomp's own extract and split on the user's discs. `gen_link.py` then takes each
  binary's `.rodata`, `.data` and data inside `.text` (ovl_0b's table) **from the user's file**, byte for byte.
  It uses splat's `.word` lines only as the relocation list, since splat types everything else as
  `.short`/`.byte`/`.asciz`. Each word is classified:
  - a function (by splat's symbol, or a raw value that is a function start) → the native function, Thumb bit set
    by the linker;
  - a raw value inside the overlay's own rodata/data → a data pointer;
  - a raw value in another binary's code that isn't a function start → kept raw and reported as "mid-code"
    (packed numbers like `0x80018000`, and the text module's internal jump tables);
  - otherwise in PS1 RAM → `psx_ram + offset`; scratchpad → `psx_scratchpad + offset`.
- Zero runs of 4 KB or more aren't stored, since `psx_ram` starts zeroed. Images: main 47 KB, ovl_0b 42 KB,
  the rest smaller.
- Nothing under `build/` is committed. The repo commits only tools and configuration.

### 3.5 PS1 RAM: one arena, by address
**All PS1 data lives at its PS1 address in `psx_ram[]`** (2 MB, `psx_mem.h`). Code is native. This one rule
removes the aliasing problem: on PS1, main's `.data` (0x800665E0–0x801EE000) contains the overlay slots and the
heap, so `g_btlUi` (0x80104F88) and the literal `0x80104000` are the same memory.
- `PSX_ADDR(a)` = `psx_ram + (a & 0x1FFFFF)`, as an integer, as the literal was.
- `build/gen/psx_syms.ld` places every data symbol the decomp knows (symbol files and splat's labels):
  `PROVIDE(g_gameState = psx_ram + 0x6794C)`. Names and literals therefore always agree. Data names need no
  namespace, since the name is the address.
- C-defined globals (a few `const` strings in ovl_0c, one 4-byte variable in ovl_0d) are ordinary ARM symbols.
  Overlay ones get the overlay prefix.
- The ~15 uses of `0x80000000` are mostly flag bits. `ovl_0a.c:6423` `p > 0x80000000` is a pointer test that
  breaks with `0x02xxxxxx` pointers: R4.

### 3.6 Link (implemented in M0)
`make` → `build/ff9ds.nds` (DSi-mode specs, `arm7_minimal` for now). The link has two passes: the first lists
what is still undefined, and `tools/gen_stubs.py` turns each into a stub that stops with its name
(`build/gen/stubs.txt` is the to-do list). On 2026-10-11 that was 280 stubs: 125 PsyQ functions (`port/psx`),
136 library/BIOS/hand-written asm routines (including the 63-function text module), and 19 game functions
still in asm. The ARM9 binary is 1.04 MB of Thumb code for all 20 binaries plus the port, and `psx_ram` is 2 MB
of bss.

## 4. PS1 compatibility layer (`port/psx/`)

| module | replaces | approach |
|---|---|---|
| `gte.c` | GTE macros and libgte (`RotTransPers`, `ratan2`, `SquareRoot0`, ...) | a software GTE with one global register file, so split macro sequences (`_b`, `_nn`, `gte_ldv0` … `gte_rtps` … `gte_stsxy`) keep their state. Integer-exact to psx-spx; the repo is GPL (D4), so a proven emulator GTE core can be adapted with attribution. `RTPS`/`RTPT`/`NCLIP`/`AVSZ`/`MVMVA` in ARM asm in ITCM, using the hardware divider |
| `gpu.c` | libgpu (`DrawOTag`, `PutDrawEnv`, `LoadImage`, `ClearImage`, ...) | the OT walker and the GX translator (§5) plus a 1 MB VRAM shadow |
| `cd.c` | libcd plus the game's CD module (`cd_startRequest`, `cd_onSectorReady`, `CdGetSector`) | sector reads from `ff9ds/discN/FF9.IMG` on SD through ARM7 file I/O, completing asynchronously on the same callbacks |
| `spu.c` | libspu plus the raw SPU registers | an emulated register file and SPU RAM shadow; voice events go to the ARM7 driver (§7) |
| `pad.c` | libpad/BIOS pad | DS keys plus touch-synthesised buttons → a PS1 digital pad report (§ dual-screen) |
| `card.c` | libcard / BIOS `_card_*` | a file-backed 128 KB memory card per slot (`ff9ds/cards/cardN.mcr`); the raw format is compatible with emulators |
| `etc.c` | `VSync`, callbacks, root counters, events | DS VBlank IRQ; root counter 2 (AKAO 240 Hz) → DS timer IRQ |
| `mdec.c` | libpress / `StSetStream` | FMV is not decoded from STR on device. The FMV entry point is overridden to play a transcoded `.fv` file (§6.3) |

A **host build** (`make pc`, 32-bit Linux, SDL2) compiles the same game code and `port/psx/` with a simple
OpenGL backend for the GX translator. It exists to iterate in seconds, debug with gdb/ASan, and run
**lockstep comparisons**: feed the same pad input to PCSX-Redux (via the decomp's `tools/emu.py`) and the host
build, and diff `g_gameState`, GameData and the RNG pointer each frame. That's how a porting bug gets told apart
from a decomp mismatch.

## 5. Graphics

### 5.1 Baseline: translate the OT (works for everything, from M2)
At `DrawOTag`, walk the ordering table in PS1 order: last slot first, and within a slot the most recently added
first. Each packet translates to DS GX commands:
- **Coordinates** are already screen-space after the GTE. An orthographic projection maps the 320-wide frame
  to 256 (×0.8, uniform). 224-line modes letterbox to 179 lines plus 6-line bars. 240-line modes come out at
  exactly 192.
- **Depth**: each emitted polygon gets `z = slot × K + seq` in draw order and a depth test of less-or-equal.
  The result matches painter's order. Translucent polygons use manual sort (`GL_TRANS_MANUALSORT`), so they draw
  in submission order.
- **Textures**: the texture page (tpage) with its colour lookup table (CLUT), keyed by (page, depth, CLUT), maps
  to a DS 256×256 texture: 4bpp → 16-colour, 8bpp → 256-colour, 15bpp → direct. The bit order and the BGR555
  colours are identical. CLUTs go to palette VRAM. A colour value of 0x0000 is transparent (DS "colour 0
  transparent"). Conversion reads from the VRAM shadow, which `LoadImage`/`MoveImage`/`ClearImage` keep dirty-tracked
  per 64×64 block. Uploads happen in VBlank and are budgeted per frame, with an LRU over texture slots.
- **Texture modulation**: PS1 0x80 = ×1.0 and up to ×2. DS tops out at ×1.0. Values above 1.0 are approximated
  with highlight shading via the toon table, or clamped (fidelity item F1).
- Lines become degenerate triangles; `TILE`/`SPRT` become quads; `DR_MODE`/`DR_TPAGE`/`DR_TWIN` update state.
- **Offscreen passes**: a `DR_AREA`/`DR_OFFSET` outside the display buffer, or a `MoveImage` from a framebuffer
  (screen capture for the battle swirl, blur, fades), is serviced by **display capture** into a VRAM bank, which is
  then used as a 256×256 direct texture. Every site still has to be classified (R7).

### 5.2 Blending
The PS1 has four semi-transparency modes. The DS 3D engine only alpha-blends.

| PS1 mode | use | DS |
|---|---|---|
| 0: B/2 + F/2 | windows, shadows | polygon alpha 16/31, exact |
| 1: B + F | glows, magic, light shafts | **2-pass**: frame N renders opaque + alpha geometry and captures it; frame N+1 renders the additive polygons on black and composites them over the captured BG with 2D blending (EVA=EVB=16, saturating = additive). That gives 30 fps on a 60 Hz panel, used only in scenes that actually use mode 1 (mostly battle effects) |
| 2: B − F | darkening effects | approximated as alpha-blended black at the source luminance (F2) |
| 3: B + F/4 | subtle glow | 2-pass additive with source colour ÷ 4 |

Fallback when the 2-pass path is off: mode 1 → alpha 50% with doubled vertex colour.

### 5.3 Native paths (M6, only where profiling says so)
1. **Field backgrounds** (`o0a func_800BE158` adds a layer's 16×16 tiles to the OT). A field layer can be
   hundreds of `SPRT_16`s, which would eat the 2048-polygon budget. Native path:
   - At field load, composite each BG layer from the VRAM shadow into a full layer image.
   - Far layers become 2D BGs on the main engine, behind the 3D layer (8bpp tiled with extended palettes, or a
     16bpp bitmap), scrolled in hardware.
   - Layers with depth (characters walk behind them) become a few large textured 3D quads at the layer's
     depth, so the depth buffer gives per-pixel occlusion, which is better than the PS1's OT bucketing.
   - Animated layers (`func_800BB090`, VRAM animation) re-upload only their changed blocks.
   - Optionally the extractor pre-scales BGs by 0.8 with a good filter, so they aren't sampled nearest-neighbour.
2. **Models**: field and battle skinned models (bone matrices on the scratchpad, `o0a` around ovl_0a.c:5430/6144,
   the `o0c` equivalent) get converted at load into DS display lists, so bones run on the DS matrix stack and the
   transform runs in hardware. The ARM9 then only does game logic.
3. **Text and windows**: rendered as 2D tiles (see `dual-screen.md`), not as GPU sprites.

### 5.4 Polygon budget
DS: 2048 polygons per frame. PS1 FF9 counts per scene aren't known yet. R9 asks for primitive counts per frame
from the emulator (field, a dense field, the world map, a normal battle, a summon, Tetra Master). If a scene goes
over, the order of fixes is: native field BG (5.3.1), merging sprite runs, then dropping or simplifying the
effects that exceed the budget.

## 6. Assets

### 6.1 Extraction (`make extract`, PC side)
Inputs: the four disc images, in raw 2352-byte `.img`/`.bin` with `.cue`/`.ccd`, or `.chd` via chdman. The
extractor checks the sha1 of `SLUS_012.51` and each `FF9.IMG`.
- Build-time outputs (`build/`): splat output → generated data (§3.4), pristine overlay `.data` images, the font
  atlas (6.4).
- Runtime folder `ff9ds/` (copied to the SD card, ~1.2 GB):
  - `discN/FF9.IMG`, read by sector exactly as the PS1 does. Later, an optional dedup pack across discs.
  - `fmv/*.fv` (6.3)
  - `cards/`
  - Phase 6: `bg/` (pre-scaled field BGs, once the BG format is identified).

### 6.2 Disc layout facts (disc 1)
`FF9.IMG` (332 MB); `OPENING/FMVD001.STR`; `SEQ01..03/FMV*.STR`; and `MBG*.MBG` (movie backgrounds: FMV played
*behind* 3D characters during field scenes). Discs 2–4 have the same layout with their own `SEQ` directories.

### 6.3 FMV and movie backgrounds
- STR (MDEC video + XA audio) goes to FFmpeg (it has the `psxstr` demuxer and the `mdec` decoder), then to the
  **FastVideoDS** encoder (`.fv`; the player decodes ~30 fps on DS and 60 on DSi, using the 3D engine for motion
  compensation and the ARM7 for SD reads and audio).
- The encoder needs an AVX2 CPU. The output is scaled from 320×(224|240) to 256×(179|192).
- Full-screen FMV plays on the top screen. The bottom screen shows chapter art or stays black (D3).
- **MBG**: frames decode into a main-engine 16bpp bitmap BG. The 3D characters render on BG0 above it, using
  the field camera. MBG sync with field scripts is R10.

### 6.4 Text
The game's font glyphs are extracted from the user's data and converted into a 4bpp tile font at two sizes:
1:1, and a ×0.8 anti-aliased version for windows on the top screen. Text stays in the game's encoding; the
renderer reproduces the opcode interpreter's layout (R15).

## 7. Audio

- **AKAO stays on the ARM9**, unchanged C, driven by a 240 Hz timer IRQ as on PS1 (root counter 2). It writes
  the emulated SPU registers.
- **SPU transfers** (`SpuWrite`/DMA) land in a 512 KB SPU RAM shadow. The shadow is scanned for ADPCM block flags
  to find sample boundaries and loop points, and each sample decodes to **PCM16** into a sample cache (DSi RAM
  allows ~2 MB worst case). Loops stay sample-exact.
- **ARM7 driver** (`arm7/`) receives voice events (key on/off, pitch, volume, ADSR parameters) over a lock-free
  ring in main RAM:
  - It maps 24 PS1 voices to 16 DS channels. When they run out, it steals the quietest released voice, or mixes
    the overflow in software on 2 channels.
  - Envelopes are software ADSR at 1 kHz using the psx-spx envelope rules.
  - Pitch converts PS1 4.12 relative to 44.1 kHz into a DS timer value. PS1 noise maps to DS noise channels
    14–15.
- **Reverb** (phase 6): capture unit 0 records the reverb-send mix, the ARM7 runs a reduced psx-spx reverb at
  16 kHz, and channel 1 plays it back. Until then it's dry.
- FMV audio is handled by the FastVideoDS player.

## 8. Memory map (DSi mode)

| region | size | |
|---|---|---|
| ARM9 code (main + 19 overlays + port) | ~1.4 MB | Thumb; hot code ARM in ITCM |
| generated data (exe + overlay `.data`/`.rodata`) | ~0.5 MB | the exe's 1.3 MB zero region at 0x800A7000 is *not* linked: it is part of the arena |
| `psx_ram` arena | 2 MB | the PS1 heap and fixed buffers at their original offsets |
| VRAM shadow | 1 MB | the PS1 VRAM image |
| texture conversion staging | 256 KB | |
| SPU RAM shadow + PCM16 sample cache | 0.5 + 2 MB | |
| FMV buffers (only during FMV) | ~1 MB | |
| SD I/O, libnds, stack, UI | ~1 MB | |
| **total** | **~9.5 MB of 16** | |

**VRAM banks** (planned layout):

The current M2 GX renderer uses A/B/C/D for textures (512 KiB), E for texture palettes (64 KiB),
and H for the debug console (4bpp font and tile map). The specialised field backgrounds
and capture effects below are not implemented yet; adding them will reduce texture capacity.

| bank | use |
|---|---|
| A, B | textures (256 KB) |
| C | textures; or the capture target for the 2-pass and offscreen effects (dynamic) |
| D | main-engine BG (field layers, MBG bitmap) |
| E | texture palettes (64 KB) |
| F/G | main OBJ and extended BG palettes |
| H/I | sub engine (bottom screen UI, 48 KB) |

**Original DS (4 MB)**: 1.4 + 0.5 + 2 + 1 + 2.5 doesn't fit. It would need DS overlays for the code, no VRAM
shadow (conversion at `LoadImage` time), offline IMA-ADPCM samples, and a smaller arena. That's a separate effort
after M7.

## 9. Repository layout (planned)

```
decomp/            git submodule: ff9-decomp, pinned
config/            overrides.txt, xrefs.txt, addr_remap.txt, ovl namespaces (generated + hand annotations)
port/include/      port_prelude.h, psx_types.h
port/psx/          gte, gpu (OT walker + GX), vram, cd, spu, pad, card, etc
port/ds/           platform: video setup, texture cache, capture, file I/O, timing, touch
port/ui/           bottom-screen UI per game mode
port/game/         overrides of game functions (loader, BG renderer, UI hooks)
port/pc/           host build backend (SDL2/OpenGL)
arm7/              SPU emulation + ADSR + reverb + FMV audio glue
tools/             extract, gen_data, find_xrefs, layoutcheck, fmv transcode, font conversion
build/  ff9ds/     generated; never committed
```

Licence: port code GPL (D4); tools CC0 like the decomp's.
