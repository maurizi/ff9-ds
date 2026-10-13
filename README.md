# ff9-ds

A port of Final Fantasy IX (PS1, NTSC-U) to the Nintendo DS family. It runs in DSi mode on DSi/3DS, uses both
screens and supports the touchscreen. It is built from the C source of the
[ff9-decomp](../ff9-decomp) matching decompilation.

This repository contains no game data. Building it and running it both require your own copy of the four NTSC-U
discs (SLUS-01251/01295/01296/01297). The build extracts what it needs from the disc images. **A built `.nds` and
the generated `ff9ds/` data folder contain Square Enix data. Never distribute or commit them.** Final Fantasy IX
© Square Enix; this project is not affiliated with or endorsed by Square Enix.

Status: **M2 in progress** (graphics). All 20 binaries from the decomp compile for ARM (DSi `.nds`) and for a 32-bit
Linux host build. Both reach the title and first field. The PC software renderer produces reference frame dumps;
the DS defaults to software rendering, with an experimental GX hardware path. The title and first room have been inspected
in melonDS's DSi mode, including candle lighting and name confirmation with zero dropped primitives.
The PC opening smoke path now passes the masked-Baku encounter and returns to
the Tantalus briefing. The software renderer now draws the battle room and all
five characters on DS after a GTE cross-product fix. Native stack-pointer
sound panning and the missing battle UI are fixed. A fresh DS software run
completes the encounter and reaches Baku’s briefing; the two pointer crashes
after the quiz are fixed; fresh DS and PC runs reach Vivi in Alexandria.
Raster optimisations raise
the measured first-battle rate from roughly 6.8 to 8.8 FPS; it is still too slow.
The GX allocator probe reports texture and geometry pressure in later scenes.
Font coverage preserves small-text strokes; VBlank bitmap swapping removes the measured naming-grid shimmer.
The candle prompt also passes consecutive-frame stability checks; the new display path’s battle performance is under validation.
Blending, offscreen effects and performance need work.
Sound is not implemented yet.
Routines the port doesn't provide yet are stubs that stop with their
name (`build/<target>/stubs.txt`). `docs/porting-notes.md` lists the PS1 behaviours the C relies on.

[Direct scenario testing](docs/scenario-testing.md) launches fields and bosses
across discs without a playthrough. Disc-3 Ark has booted directly on host and
DS; encounter coverage and battle effect support remain in progress.

## Building

Requires Docker, Python 3, and raw 2352-byte images of the four discs at `disks/us/raw/FF9 CD1.img` …
`FF9 CD4.img` (the same layout as ff9-decomp).

```
git submodule update --init          # decomp/ (ff9-decomp)
make extract                         # the decomp's extract + split, on your discs
tools/docker.sh make -j16            # -> build/ff9ds.nds
tools/docker.sh make -j16 pc         # -> build/pc/ff9ds (host build; --help for its options)
tools/docker.sh make test-gx         # host texture/cache/compositing checks with ASan and UBSan; no disc data needed
```

`tools/docker.sh` runs the BlocksDS container (`skylyrac/blocksds:slim-v1.24.0` plus Python). The build never
writes game data outside `build/`.

The DS defaults to software rendering to preserve textures and framebuffer effects. It is still CPU-bound.
Use `tools/docker.sh make ds SOFTDISPLAY=0` for the experimental GX path, which has known missing textures,
geometry drops and lighting artifacts. Changing renderer or scripted-input options rebuilds the DS port
objects automatically. `GXEXPERIMENTAL=1` enables renderer experiments that currently regress DS image quality; it is off by default. See `docs/porting-notes.md` for emulator and reference-frame recipes.

| doc | contents |
|---|---|
| [docs/design.md](docs/design.md) | architecture: how decomp C becomes an ARM binary, the PS1 compatibility layer, the asset pipeline, memory and VRAM budgets |
| [docs/dual-screen.md](docs/dual-screen.md) | screen layout, touch and button mapping for every game mode |
| [docs/roadmap.md](docs/roadmap.md) | milestones, exit criteria and risks |
| [docs/decomp-requests.md](docs/decomp-requests.md) | what the port needs identified or written on the decomp side, by priority |
| [docs/decisions.md](docs/decisions.md) | open decisions and the default each one has until decided |
| [docs/porting-notes.md](docs/porting-notes.md) | PS1 behaviours the decomp's C relies on, and how the port handles each |
