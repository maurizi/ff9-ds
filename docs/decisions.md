# Open decisions

Each has a default that the design assumes until it's decided otherwise.

| # | decision | default | trade-off |
|---|---|---|---|
| D1 | target hardware | **decided 2026-10-11: DSi mode** (DSi, 3DS, DSi-capable flashcarts); original DS is a stretch goal after M7 | 16 MB / 134 MHz vs 4 MB / 67 MHz. The original DS needs a different memory architecture (code overlays, no VRAM shadow, offline sample conversion) and probably simpler effects |
| D2 | where field dialogue goes | top screen, in place, ×0.8 AA font; fall back to bottom screen at 1:1 if the M5 prototype isn't legible | positional context vs legibility |
| D3 | bottom screen during FMV | black | black vs chapter art / location card (art would have to come from the disc too) |
| D4 | licence of port code | **decided 2026-10-11: GPL** (GPL-3.0-or-later unless reused code needs GPL-2.0-only); emulator GTE/SPU code may be reused with attribution | permissive (MIT/CC0, like the decomp's tools) means writing GTE/SPU from psx-spx; GPL would allow reusing emulator code (Beetle/PCSX GTE, SPU ADSR/reverb) |
| D5 | scaling | uniform ×0.8 with letterbox in 224-line modes | vs ×0.857 vertical (7% squash, no bars) or cropping with camera follow |
| D6 | how the decomp is consumed | git submodule pinned per port release | vs a path to the live checkout (faster, less reproducible) |
| D7 | equivalent-C location | `src_equiv/` in the decomp repo | vs keeping it in the port repo (no decomp-repo churn, but drifts from the headers) |
