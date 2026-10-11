# CLAUDE.md

FF9 (PS1 NTSC-U) port to Nintendo DS (DSi mode by default), built from the ff9-decomp C source at
`../ff9-decomp` (to become the `decomp/` submodule). Design is in `docs/`; read `docs/design.md` first.

## Rules
- Never commit or embed game data: no disc contents, extracted blobs, generated data, converted FMV/fonts/BGs,
  built `.nds`. Everything derived from the disc goes in `build/` or `ff9ds/` (both gitignored).
- Never edit `decomp/` sources. Change behaviour through the prelude, PsyQ replacements or link-time overrides
  (`config/overrides.txt`), in that order (design §3.1). Non-portable decomp code goes to the decomp side as a
  request in `docs/decomp-requests.md`. Such a change must keep the PS1 `make verify` OK.
- Anything the port needs identified in the decomp goes in `docs/decomp-requests.md` (R#), not guessed here.
