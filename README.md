# ff9-ds

A port of Final Fantasy IX (PS1, NTSC-U) to the Nintendo DS family. It runs in DSi mode on DSi/3DS, uses both
screens and supports the touchscreen. It is built from the C source of the
[ff9-decomp](../ff9-decomp) matching decompilation.

This repository contains no game data. Building it and running it both require your own copy of the four NTSC-U
discs (SLUS-01251/01295/01296/01297). The build extracts what it needs from the disc images. **A built `.nds` and
the generated `ff9ds/` data folder contain Square Enix data. Never distribute or commit them.** Final Fantasy IX
© Square Enix; this project is not affiliated with or endorsed by Square Enix.

Status: **M1** (headless boot). All 20 binaries from the decomp compile for ARM (DSi `.nds`) and for a 32-bit
Linux host build. On the host build the game boots, plays the opening and title, takes New Game and runs the first
field (headless: no picture or sound yet). Routines the port doesn't provide yet are stubs that stop with their
name (`build/<target>/stubs.txt`). `docs/porting-notes.md` lists the PS1 behaviours the C relies on.

## Building

Requires Docker, Python 3, and raw 2352-byte images of the four discs at `disks/us/raw/FF9 CD1.img` …
`FF9 CD4.img` (the same layout as ff9-decomp).

```
git submodule update --init          # decomp/ (ff9-decomp)
make extract                         # the decomp's extract + split, on your discs
tools/docker.sh make -j16            # -> build/ff9ds.nds
tools/docker.sh make -j16 pc         # -> build/pc/ff9ds (host build; --help for its options)
```

`tools/docker.sh` runs the BlocksDS container (`skylyrac/blocksds:slim-v1.24.0` plus Python). The build never
writes game data outside `build/`.

| doc | contents |
|---|---|
| [docs/design.md](docs/design.md) | architecture: how decomp C becomes an ARM binary, the PS1 compatibility layer, the asset pipeline, memory and VRAM budgets |
| [docs/dual-screen.md](docs/dual-screen.md) | screen layout, touch and button mapping for every game mode |
| [docs/roadmap.md](docs/roadmap.md) | milestones, exit criteria and risks |
| [docs/decomp-requests.md](docs/decomp-requests.md) | what the port needs identified or written on the decomp side, by priority |
| [docs/decisions.md](docs/decisions.md) | open decisions and the default each one has until decided |
| [docs/porting-notes.md](docs/porting-notes.md) | PS1 behaviours the decomp's C relies on, and how the port handles each |
