# Roadmap

Each milestone has an exit check that can be demonstrated. Decomp requests (`R#`, see `decomp-requests.md`) are
listed at the milestone that first needs them. The decomp agent should work slightly ahead of the port.

## M0: Scaffold (no decomp requests needed) — done 2026-10-11 (DeSmuME: boots, enters the game loop, stops at the first stub)
- Set up the BlocksDS toolchain in Docker (like the decomp's), with `decomp/` as a submodule and `make extract`
  reusing the decomp's disc tools.
- `tools/gen_data.py`, `tools/find_xrefs.py`, `tools/layoutcheck.py`, plus overlay symbol namespacing.
- Every C file of the decomp compiles for ARM. Unresolved symbols are reported as a list, never stubbed silently.
- **Exit**: an `.nds` that links (with stubs for R1 functions), boots in melonDS and no$gba, and prints the
  `g_gameState` address. The host build links too.

## M1: Headless boot (R1, R2, R3, R4, R10-cd) — PC build and DSi (melonDS) reach and run field 70 (2026-10-11); lockstep pending
- Software GTE with a unit test suite (vectors from PCSX-Redux traces of GTE ops via `tools/emu.py`).
- `port/psx`: CD from SD, VSync/timers/events, pad, card; GPU and SPU are no-ops.
- **Exit**: the host build reaches the title screen, starts a New Game and enters the first field. A lockstep run
  against PCSX-Redux with the same input matches `g_gameState`/GameData/RNG for 3000 frames. On DS: same
  progress, checked via debug prints.

## M2: Pictures (R5, R7, R8, R9)
- OT walker → GX; VRAM shadow; texture/CLUT cache; scale/letterbox; capture-based offscreen passes.
- **Exit**: Alexandria fields, a battle and the world map render recognisably on DS at playable speed. Primitive
  and polygon counters are on the bottom-screen debug overlay. The profile goes to `docs/perf.md`.

## M3: Sound (R11)
- Emulated SPU on ARM9 → ARM7 driver: voice mapping, software ADSR, PCM16 sample cache.
- **Exit**: the title music and the field/battle music play and sound right compared with an emulator, side by
  side; menu SFX play; nothing hangs on key-off.

## M4: Whole game, PS1 presentation (R10, R12, R13, R14)
- FMV and MBG via FastVideoDS; disc changes; saves; every mode reachable.
- **Exit**: a scripted route (save states from the decomp's emulator collection, re-created as `.mcr` saves)
  enters every mode and every menu overlay. Tetra Master and Blackjack are playable. Save, quit and reload work.

## M5: Native dual-screen UI (R15–R21), one mode at a time
Order: battle (biggest win: frees the view), then dialogue choices and field context buttons, then menus, the
world minimap, Tetra Master, name entry, save/load, and finally the mini-games.
- **Exit per mode**: everything doable by touch and by buttons; no PS1 UI left on top; the text overflow report is
  clean for that mode's banks.

## M6: Performance and fidelity (R18, R22, profiling-driven)
- Native field BG, native model renderer, 2-pass additive blending, reverb, pre-scaled BGs.
- **Exit**: field at the PS1 frame rate everywhere; battle effects at least 20 fps; no scene over the polygon
  budget.

## M7: Validation
- A full playthrough of all four discs, by people and by long scripted runs (stress: Trance, summons, the
  Memoria/Crystal World dungeons, Chocobo and card games). A bug tracker is kept separately from decomp issues.
- Stretch: original DS (4 MB) build.

## Risks, in order of concern

| risk | why | mitigation |
|---|---|---|
| polygon budget (2048/frame) | PS1 BG tiles and effects are polygon-hungry | measure first (R9); native field BG; effect simplification |
| additive blending | magic effects depend on it | 2-pass capture; fallback approximations |
| CPU (software GTE plus OT translation) | ARM9 has no GTE; main RAM is slow | DSi 134 MHz, ITCM asm hot paths, hardware-transform models |
| hand-written asm (R1) | ~150 routines need equivalent C before anything runs end-to-end | decomp-side work starts now, M0/M1 stubs in the meantime |
| texture upload bandwidth | VRAM writes only in VBlank (~15–20 KB per frame) | dirty tracking by 64×64 block; uploads at scene load; LRU |
| text fitting | 320 → 256 px | the measured overflow report; 1:1 font on the bottom screen |
| legal | game data in builds | data only from the user's disc; built `.nds` never distributed; no committed assets |
