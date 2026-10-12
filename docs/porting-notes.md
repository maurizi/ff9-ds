# Porting notes: PS1 behaviours the decomp's C relies on

Matching C reproduces the PS1 binary exactly, so it also reproduces things that only work because the code runs on
a MIPS R3000 with PS1 memory. The port finds them one crash at a time. Each class below has a general fix, and an
upstream request where the decomp can make the C say what it means without changing the PS1 output.

| class | example | why it works on the PS1 | port fix | upstream |
|---|---|---|---|---|
| `void` function whose result is used | `D_80067948 = func_800AD44C(...)` | `$v0` still holds the last callee's result | real return type | fixed at the source (decomp `tools/void_returns.py`, 15ec54c) |
| argument passed through in a register | `func_800AA3F4(void)` calls `func_800AAEF8()` | `$a0` still holds the caller's argument | the port defines `REG_PASSTHROUGH(x)` as `x` | fixed at the source (decomp `tools/arg_check.py`, 1b41477) |
| pointer-ness tested by sign | `if ((s32)g >= 0)` = "still a file offset" | RAM pointers are 0x80xxxxxx, negative | the port defines `IS_OFFSET(x)` as "not a pointer into psx_ram" | `IS_OFFSET()` at all 16 sites (decomp 386d31f) |
| pointer-ness tested against 0x80000000 | `if (p > 0x80000000)` | same | `PORT_IS_RAM_PTR()` (patch) | listed in R4 |
| read through NULL | `D_8007F790->unk3C` with `D_8007F790 == NULL` | low RAM is readable | `PSX_NZ()` (patch); DS: zeroed ITCM head (planned) | none: original behaviour |
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
- `make ds AUTORUN=1 AUTOPRESS="CROSS@2500,UP@2900+3,CROSS@3000+3"` runs the title-to-New-Game script with no input.
  The status line at the bottom of the console shows frame, mode, overlays and libcd state.
