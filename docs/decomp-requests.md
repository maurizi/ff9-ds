# Requests to the decomp side

What the port needs from `ff9-decomp`, in priority order. **P0 blocks the first ARM build, P1 blocks rendering
and audio bring-up, P2 blocks the native dual-screen UI, and P3 is optimisation.**

General rules for the decomp agent:
- Everything here can be delivered without breaking `make verify`. Port-only C (functionally equivalent,
  non-matching) goes in a separate tree that the PS1 build never compiles. `src_equiv/<binary>/` is proposed.
- Names follow `docs/naming.md`, with evidence tags as usual. For the port, **identifying the boundary**
  (entry point, state struct, input read) matters more than naming every helper inside it.
- Runtime work goes through the game-tester workflow (one emulator at a time). Items marked 🎮 need the emulator.

---

## P0: needed to build and boot

### R1. Equivalent C for every remaining asm routine
The port has no MIPS. Every routine below needs a C body that behaves the same; it doesn't have to match. Where a
routine uses a custom register convention, document that convention and give it a normal C signature instead.

| area | routines | notes |
|---|---|---|
| main text/glyph module `0x80050F4C–0x80053924` | 63 (`func_80050F4C` … `func_80053764`) | global registers `$t5/$s6/$s7`, no ABI, some GTE. This is the dialogue/window text renderer (the opcode interpreter at main.c:~14140 calls `func_80050F4C(colour)` / `func_800512C8` / `func_800512B4`). **Highest value**: the port's native text renderer (R15) needs its semantics anyway |
| `text_setBank`, `text_get`, `text_getInfo`, `text_getFrom` | 4 | trivial |
| sin/cos/rand module `func_8004BB7C`–`func_8004BF74` (with `rand_refill`) | 7 | fall-through entry points; cos/sin table lookups |
| `func_80064C24` | 1 | calls `func_800512E4` with the register ABI |
| `func_80062C8C` (libgte hand-written) | 1 | name it if it's a known libgte routine; the port provides libgte |
| ovl_0a | `func_800C1DC4/DE0/DF4/E04`, `func_800B5958`, `func_800C9AB8` | |
| ovl_0b | 47, including the `CF5xx–CFAxx` / `DFE4C–E0014` blocks | battle |
| ovl_0c | 16 (`func_800B3844` … `func_800B6598`, `func_800CB1EC`, `func_800C709C`) | world map, probably terrain/GTE inner loops |
| the 16 non-matching game functions | main `8002E718 800475D8 8004DAD0 8005681C 80059414`; ovl_0a `800B4A5C 800BD130 800C0038 800C07D8 800C65CC`; ovl_0b `800E4A3C 800BFF20 800C3F78 800DA138`; ovl_0d `800AC708`; ovl_37 `801F13F8` | drafts in `docs/wip/`: say which ones are **behaviourally** correct (register allocation only) so the port can use them now |

Please also confirm that no *game* code is hiding in `config/library_funcs.us.txt` apart from the above
(`func_80064C24` "out of scope" for example).

> **ff9-decomp, partial (commit 824d1e6 and earlier):**
> - done: main text/glyph module `0x80050F4C–0x80053924`, all 63 routines, in `src_equiv/main/script_text.c`.
>   It's not only text: it is the pen/glyph emitter, the debug printf, the **event-script expression evaluator**
>   (`func_80051F54`), variable read/write (`func_80051D4C`/`func_80051E8C`), the **script scheduler**
>   (`func_80052870`: runs each object's script, handles jumps/switches/waits itself, everything else goes to
>   `evt_execInstr`), geometry (atan, length, isqrt, point-in-polygon, `ratan2`) and two streak-particle pools.
>   The register convention ($t4–$t7 pen state, $t8 glyph sink, $s5/$s6/$s7 script flags/ip/stack) is
>   documented at the top of the file and turned into file-static state; every function has a normal C
>   signature matching `include/`. Math helpers were replayed against emulator captures (0 differences); the rest
>   is checked against the asm by reading.
>   One port hazard: evaluator opcodes 0x60/0x61 take an object pointer that went through the 26-bit stack and
>   restore it with `| 0x80000000` (commented in the code).
> - done: `text_setBank/text_get/text_getInfo/text_getFrom` → `src_equiv/main/text_bank.c`.
> - done: sin/cos/rand block → `src_equiv/main/math_rand.c` (`math_sin`, `math_cos`, `rand_refill`, the
>   random-byte routine). `func_8004BBBC/BBD0/BBDC` are jump targets inside `math_sin`, not functions; nothing
>   calls them. `rand_refill` was checked byte-for-byte against a real refill.
> - done: `func_80064C24` (pen colour preset) → end of `script_text.c`.
> - `func_80062C8C`: a file-local helper of libgte MSC02.OBJ (no name in the signatures), called only by
>   `VectorNormal`, `VectorNormalS`, `VectorNormalSS` and `MatrixNormal`. The port's libgte replaces those, so it
>   needs no equivalent.
> - open: the ovl_0a/0b/0c asm routines, the 16 non-matching drafts, and the library_funcs audit.

### R2. Hardware-touching game code
List every game function (outside PsyQ) that reads or writes hardware registers or DMA directly: the SPU (the AKAO
`SPU_REG16`/`SPU_VOICE_REG` users, main.c ~17300–17400 and ~19670), GPU (`GPU_cw`), CD, root counters,
interrupt masks. For each, say read or write, which registers, and why. The port reimplements these at that level.

### R3. Cross-binary pointers
The port links all overlays into one binary, each in its own namespace. It has to know which overlay every raw
address belongs to.
- **Main's mode loop** (`func_800127A0`): which overlay each entry belongs to: `func_800A7088` (mode 0),
  `func_800A7A48` (1), `func_800A859C` (2), `func_800A8860` (3), `func_800A713C` (4), `func_800A80A4` (7),
  `func_801EF284` (8), `func_800A752C` (9). Also: modes 7 and 8 both load file 101 (`0x65`) but call different
  addresses in different slots. What are modes 7 and 8?
- **Data tables** holding raw overlay addresses, e.g. `D_8006C9D4` (menu overlay init/update table: `0x801F3874`,
  `0x801F4748`, ...), indexed through `D_8006CA40` (overlay file ids `0x3A 0x3C 0x3B 0x39 0x32 0x3D` ...). File
  `0x3D` isn't in the code-overlay list: what is it?
- The port will generate candidates with `tools/find_xrefs.py`: every `.word` in any binary's data in
  0x80010000–0x80200000 that splat didn't symbolise, plus every `jal`/`la` target outside its own binary. The
  request: **confirm the ambiguous ones**. A `binary:addr → ovl` list in `config/` would be ideal.

### R4. Fixed RAM addresses
For each literal below: what the buffer is, its size and its lifetime. The port maps them into an arena, so it
has to know about overlap with linked symbols.

| address | where | |
|---|---|---|
| `0x801FCB00` (14 uses) | ovl_39 | save/load buffer? |
| `0x801E7700` (9), `0x801F1700`, `0x801EC700` | ovl_0b_2 | |
| `0x80104000`, `0x80104A00`, `0x80103800`, `0x80103C00` | ovl_0d | Tetra Master |
| `0x801CDF00` | ovl_14, ovl_3e | |
| `0x801EDF00` | main | menu overlay slot, also used as a buffer by `func_80028B20` |
| the rest | `grep -rnoE '0x80[0-9A-F]{6}' src` | 33 distinct |

Also, is **`ovl_0a.c:6423` `if (p > 0x80000000)`** a pointer-validity test? On DS, pointers are `0x02xxxxxx`.
Please check the other 15 `0x80000000` uses quickly; most look like flag bits (`statusImmune`, the RNG).

---

## P1: rendering, audio, files

### R5. Frame structure per mode 🎮
For each mode (field, battle, world, menu, Tetra Master, opening): the frame begin and end
(`gfx_selectBuffer`, the `DrawOTag`/`PutDrawEnv`/`PutDispEnv` call sites), the **display resolution**
(320×224 or 240? interlaced anywhere?), OT length, and the frame rate actually achieved (VSync interval: 30 or
20/15 in battle?).

### R7. VRAM traffic classification
Classify every `StoreImage` (34), `MoveImage` (30) and `LoadImage` (88) site, and every DR_AREA/offscreen draw,
as one of:
**(a)** loading textures or CLUTs at scene load, **(b)** per-frame texture or CLUT animation, **(c)** reading or
copying the framebuffer (battle-entry swirl, blur, fade, screenshots for the menu background), **(d)** rendering
to an offscreen area and using it as a texture. The port handles (c) and (d) with display capture, so it needs
the exact sites.

### R8. Semi-transparency usage 🎮
Count primitives per frame by blend mode (0–3) in: a normal field, a battle with spells (Fire/Thunder/Cure), a
summon (any Eidolon), Trance, and the world map. Also the subsystems that set modes 1 and 2. This decides the
scope of the 2-pass additive path.

### R9. Polygon budget measurement 🎮
Count primitives per frame by type (POLY_F3/FT3/G3/GT3/F4/FT4/G4/GT4, SPRT/SPRT_16/TILE, LINE) and the number
of `RTPS`/`RTPT` per frame, in: Alexandria main street, a dense field (Treno, Lindblum), the world map from the
airship, a 4v1 battle, a summon, Tetra Master. Hooking `DrawOTag` and walking the OT in the emulator is enough.
The numbers go in the port's `docs/perf.md`.

### R10. File, stream and disc layer
- The game's CD module API: `cd_startRequest`, `g_cdLoad`, `func_80022C60` (load(sector, size, dest,
  callback)?), `func_80022B18` (busy?), `func_8001E218` (file lookup by dir/id), `g_gameState->unk20`
  (FF9.IMG base sector?).
- **The FMV player in play** (ovl_64 is the debug one): which code plays STR files and MBG movie backgrounds, how
  an event script triggers it, and how MBG frames sync with field scripts.
- Disc-change prompt and check code.
- Any XA audio streaming outside FMV.

### R11. Sound boundary
- The full `akao_command` command set: which commands game code issues (music play/stop/fade, SFX, volume).
- How sample banks reach SPU RAM (`SpuWrite`/transfer callers, file type, and when).
- Does any game code **read** SPU registers or voice status (key status, envelope)? The port only needs to
  emulate what is read.

### R12. Pad layer
- The raw → logical bit mapping (`0x20000` = confirm), and where button configuration is applied.
- Per mode, where L2/R2/Select/Start/□ are read (flee, ATE, help, cards, pause) and whether analog input is used.

### R13. Memory card
The ovl_39/libcard path: confirm it only uses BIOS `_card_*` and libcard (so a file-backed 128 KB card works),
plus the save block layout boundary (one save = which blocks).

### R14. Timing
Uses of `GetRCnt`/root counters and `VSync(-1)` timestamps outside libetc and AKAO, and whether anything depends
on the exact PS1 timing.

---

## P2: dual-screen UI. For each item: (a) draw site, (b) state, (c) input read

### R15. Messages and windows
`msg_open(…, layout, …)` (layout semantics), the `Window` struct, window position and size computation, the text
opcode interpreter (main.c ~14100–14400: colours, variables, waits, icons), and **choice selection**: option
count, the cursor index variable, and how the result is returned to the event script. Also the field "!"/"?"
prompt icons and the ATE prompt.

### R16. Battle UI
`menuState` / `cmdUnit` (already found) plus: the command list per unit, sub-menus (Skill, Item, Summon lists),
the target cursor and `targets` bitmask selection, the ATB/HP/MP/Trance HUD draw function(s), the help window,
the battle message banner, and where the menu reads the pad.

### R17. Menu overlays
For each of `32 33 34 36 37 38 39 3a 3b 3c 3e 65` and the top-level main menu (which lives in the exe or ovl_0a,
per the runtime findings): the per-frame update and draw functions, cursor and list state, and the text banks.
`3c` and `3e` purposes are still unknown.

### R18. Field
The player movement input point (where the d-pad becomes a direction), the camera (`func_800BF660`), the BG layer
system (`func_800BE158`, `BgTile`, `BgSprSet`, `func_800BB090` animations, scroll wrap), the walkmesh (only if
tap-to-walk is ever wanted), the location-name display, and the encounter flag.

### R19. World map
The minimap draw (it's toggled by Select on PS1), the player position and heading in map cells, the vehicle state
(chocobo/airship, ascend/descend input), and the place-name lookup.

### R20. Mini-games and event-script input
The event script opcodes that read the pad (`evt_execInstr` opcode table), so on-screen buttons can feed them:
jump rope, the Hippaul race, Chocobo Hot & Cold digging, Mognet deliveries.

### R21. Tetra Master and Blackjack
ovl_0d: board state, both hands, the selected card and cell, turn phase, where input is read. ovl_15: the action
selection.

---

## P3: performance

### R22. Model formats and renderers
The field and battle model formats (vertices, bones, primitive lists), the model draw functions (ovl_0a around
ovl_0a.c:5430/6144, the battle equivalent, ovl_0c), and the animation format. This enables converting models to
DS display lists with hardware transform.

---

## Open findings from the port (queued while the decomp session was offline)

### F1 (2026-10-11). Definition/declaration mismatch that tools/void_returns.py misses
ovl_0a.c:3687 defines `void func_800B2CC0(s32, s32, s32, s32)` (it calls func_800B31B8), but ff9.h:3535 declares
`s32 func_800B2CC0(...)`, and main's script opcode 0x8F uses the result (`if (func_800B2CC0(...)) { retry }`). On
MIPS, $v0 carries func_800B31B8's result. Off MIPS the value is garbage, and the opening scene's script retries
forever. The tool trusts the caller's declaration (s32). ff9.h hides main-only declarations from overlay files,
so gcc never sees the conflict.
- (a) Fix: `s32` plus `return func_800B31B8(...)`. The port patches it at build time until then.
- (b) Extend the check to compare every definition with every declaration of the same symbol, in any binary or
  header (return type and parameter count).

### F2 (2026-10-12). Player movement reads through a NULL collision result
The draft `docs/wip/main_800475D8.c` calls `func_80038618(obj, 0, &dist)` and reads `o->unkF`
before checking `o != NULL`. Moving toward the candle in opening field 70 reaches `o == NULL`;
the PC faults at address 0x0F and the DS data-aborts. The original assembly confirms the unconditional
`lbu v0, 0xF(s3)` at 0x800478D8, followed by the NULL check at 0x800478FC. The port maps only this
read through `PSX_NZ(o)` with a counted build-time patch, preserving the subsequent NULL collision test.
This is another low-RAM read dependency, not grounds for inserting a NULL guard into matching PS1 code.
Please include this site when documenting movement/collision semantics (R18).

### F3 (2026-10-12). Battle overlay restored from cache bypasses sector relocation
Entering battle 336 restores overlay 0B with `func_80012900` from cached RAM/VRAM.
A PC watchpoint on `D_800F78A4` shows `StoreImage` restoring raw `0x800F7870`, then
`func_800E4098` faults when it uses that pointer. The sector callback did not run for
this prefix. The port now relocates the restored prefix immediately after either
cache path; later sectors retain their normal relocation. This is a port loader
requirement, not a request to change the matching PS1 loader.

### F4 (2026-10-12). Three battle initialization dependencies on low RAM
Confirmed against a running PSX New Game, after the masked Baku entrance:
- `func_800EA004`: `g_btlUi` at `0x80104F88` is NULL before allocation; the menu-state
  byte at physical `0x12B4` is zero. The port maps this query through `PSX_NZ`.
- `func_800BBD3C`: Baku's model at `0x8010AF08` has `unk50 == NULL`. The original
  writes texture-animation record 2's fields through that pointer (`0x2C`, `0x34`,
  `0x3A`). The port maps those three writes into its low-RAM shadow.
- `func_800BC78C`: `Battle.curCmd` at `0x80109888` is NULL while selecting idle
  animation. It still reads the unit pointer at physical `0x4`; on the PSX this
  contains `0x275A0C80`, which does not equal any battle unit. The port maps the
  corresponding unit/command-number reads through `PSX_NZ`.
These are original low-RAM dependencies. The port does not have a complete BIOS
kernel RAM image; its shadow preserves addressability and subsequent writes, not
all initial kernel instruction bytes. Please include these sites in R4/R16.

### R1 follow-up (2026-10-12). Battle handwritten helpers implemented in the port
`port/game/battle_math.c` supplies nine scalar entries at CF560/574/58C/690/6B8/
708/748/788/9DC. CF588 is mutable code-adjacent data, despite its function label.
CF58C stores the background OT shift at CF5A0. The RNG seed is scratchpad +0x3FC.
`battle_background.c` implements CEED4/CF448 (background packet emission and packed
vertex projection). `battle_pose.c` implements C7DAC (packed animation decoding and
bone hierarchy composition). `battle_packets.c` implements C58E4 (eight mesh
primitive formats, active OT insertion and packet-buffer end marker).
Please resolve names/data labels upstream while preserving PS1 output. The port
keeps these as strong native definitions over weak missing-function stubs.

### F5 (2026-10-12). E4A3C WIP stack-home struct is not a native calling convention
`docs/wip/ovl_0b_800E4A3C.c` gives the third argument the by-value `E4Arg`
type, whereas its header and callers pass two pointers as arguments 3 and 4.
The draft immediately assigns `b = a; c = b`, discarding its other incoming
fields. A counted port patch reconstructs the local draft struct from the native
four-argument signature. Please reconcile the upstream declaration/draft while
retaining the PS1 instruction output.

### F6 (2026-10-12). Battle text-size call spells out a MIPS hidden return pointer
Overlay 0B declares `func_8002AF70(void *, s32)` and passes `&v.unk0` as its
first argument. The resident definition returns the four-byte `TextSize` struct
from a single string argument. On native x86, the mismatched call treats the
four-byte local result as the string and writes its hidden return to the wrong
stack address; battle help setup aborts in `__stack_chk_fail`, inlined into
`func_800F4F44`. The port redirects this one call through
`port_measure_text_into`, compiled with the resident struct-return declaration.
Please document this ABI boundary rather than treating the overlay prototype as
the logical function signature.

### R1 follow-up: effect helpers
Native implementations now also cover E3EEC (effect-frame orchestration),
CF808/91C/8EC (orientation and transforms), CF9B0/CFA00 (scalar/vector GTE
interpolation), DFE4C/DFE54 (shared particle integration with two drag offsets),
and D4478/D40CC (projected textured rectangles and sprite batches). D4478 tests
existing MAC0 after RTPT and exits the whole batch on zero; it does not issue
NCLIP. DFE4C branches into DFE54's common body. Gravity updates stored velocity
without updating the IR used for the current position step. Please retain these
assembly details during naming and equivalent-C work.

### F7 (2026-10-12). Mode-8 dispatcher retains a renamed numeric entry
After battle the resident dispatcher still calls `func_801EF284`, while overlay
65 defines `battleEnd_main` at that address. This was a missing native function
stub despite a complete C entry body. A counted port patch uses the current name;
`config/xrefs.txt` pins it to overlay 65 because mode 8 explicitly loads file 101.
Please update the resident call/prototype upstream to the named entry. Reaching
mode 8 only establishes post-battle dispatch; it does not distinguish victory
from game over or establish visual correctness.

### F8 (2026-10-12). Battle positional sound accepts either a unit ID or a vector pointer
`func_800E976C` projects a unit position or an explicit `SVECTOR`, then maps screen
X around 160 to a sound-pan value around 128. `func_800C2200` passes a stack vector
when dispatching battle sound through `func_800AD90C`. The original
`arg0 & 0x90000000` discriminator identifies PS1 pointers but misses native ARM
stack addresses. The real DS opening battle halted with SystemError('S', 310):
matched savestate/DTCM return addresses trace `func_800C2200` -> `func_800AD90C` ->
`func_800E976C` -> `func_800E750C` -> `func_800E716C`'s unit-index check.
A counted port patch distinguishes small IDs (0..17, with -1 handled first) from
native pointers above the 2 MiB PS1 image. Please name/document this tagged
argument and sound-pan conversion; preserve the original PS1 address-bit test
for matching code generation. No decomp source was edited.

### Ordering-table links followed by the CPU (2026-10-12)

`docs/wip/ovl_0a_800B4A5C.c` follows `g_gameState->unk1C->unk10[buffer][8]` as a pointer before inserting the full-screen TIM sprite and texture-page packets. Original MIPS at 0x800B4DC4/0x800B4DCC and 0x800B4E40/0x800B4E48 confirms that it reads the stored link, rather than taking the address of bucket 8. This is valid in the PSX physical RAM mirror. Native 24-bit OT links must have the arena high byte restored before CPU access, exactly as in the GPU walker. The port applies a counted two-site `PSX_OT_PTR` patch; please annotate this low-RAM alias behavior when naming this routine.


### F9 (2026-10-13). KO and status controls write through absent texture-animation sets

Additional optional-motion evidence: direct disc-3 Ark startup selects Steiner
model 0x028F, whose default-motion ID is 0xFFFF. AB810 nevertheless reads the
absent motion header (`lw` at 800AB8F8) and relocates its low-RAM fields. The
port maps the local dereference, preserving the model's NULL state. Please
document this optional default motion alongside optional texture animations.

Native DS first-fault capture identifies `func_800BD5A0` when Marcus dies. Marcus and Blank have no texture-animation set (`BtlUnit9C.unk50 == NULL`). Original disc MIPS at 0x800BD5E0/600/614/628 performs unconditional stores at +0x2c, +4, +0xc and +0x12. Low RAM is valid on PSX; on DS these addresses alias IRQ/raster instructions. This extends F4 beyond Baku's initialization. The port maps reviewed texture-animation dereferences into its low-RAM shadow while preserving the NULL loader state and matching decomp source. Related status, revival, script and summon accesses are inventoried together (36 dereferences across ten routines). Please annotate the optional animation-set/low-RAM behavior when naming these routines; do not insert guards into matching PSX code merely to make native execution safe.
