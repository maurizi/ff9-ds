"""Symbol and layout knowledge from the decomp checkout (decomp/), shared by the port's build tools.

Everything here is read from the decomp's committed configs and from its splat output (asm/, generated from the
user's disc by `make extract`). Nothing is hard-coded except the boot exe's section offsets, which are fixed facts
of SLUS_012.51 (its sha1 is checked by the decomp's `make extract`).
"""
import re
from pathlib import Path

DECOMP = Path("decomp")
VERSION = "us"

# SLUS_012.51: 0x800-byte header, then .rodata, .text, .data (decomp CLAUDE.md "Layout facts").
MAIN_FILE = DECOMP / "disks/us/SLUS_012.51"
MAIN_VRAM = 0x80010000
MAIN_HEADER = 0x800
MAIN_TEXT = 0x2FA0
MAIN_DATA = 0x56DE0

RAM_LO, RAM_HI = 0x80010000, 0x80200000  # PS1 RAM the game uses (below 0x80010000 is the BIOS/kernel)
SPAD_LO, SPAD_HI = 0x1F800000, 0x1F800400

SYM_LINE = re.compile(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)$")
ADDR_NAME = re.compile(r"^(?:func|D|jtbl)_([0-9A-F]{8})$")


class Binary:
    """One of the decomp's 20 binaries: its file, load address and section ranges (file offsets)."""

    def __init__(self, name, path, vram, rodata, text, data, header=0):
        self.name, self.path, self.vram = name, path, vram
        self.header = header
        self.rodata, self.text, self.data = rodata, text, data  # file offsets
        self.bytes = path.read_bytes()
        self.end = len(self.bytes)
        self.ns = "" if name == "main" else "o" + name[4:] + "_"  # overlay namespace prefix: ovl_0b -> o0b_

    def vram_of(self, off):
        return self.vram + off - self.header

    def off_of(self, vram):
        return vram - self.vram + self.header

    def text_range(self):
        return self.vram_of(self.text), self.vram_of(self.data)


def binaries():
    out = {"main": Binary("main", MAIN_FILE, MAIN_VRAM, MAIN_HEADER, MAIN_TEXT, MAIN_DATA, header=MAIN_HEADER)}
    for line in (DECOMP / f"config/overlays.{VERSION}.txt").read_text().splitlines():
        f = line.split("#")[0].split()
        if len(f) >= 4:
            name = f"ovl_{f[0]}"
            out[name] = Binary(name, DECOMP / f"disks/us/ovl/{f[0]}.bin", int(f[1], 16), 0, int(f[2], 16), int(f[3], 16))
    return out


def symbol_files(binary):
    """The decomp's symbol files that describe `binary`, hand-written first."""
    cfg = DECOMP / "config"
    if binary == "main":
        names = [f"symbols.{VERSION}.txt", f"symbols.psyq.{VERSION}.txt",
                 f"undefined_funcs_auto.{VERSION}.txt", f"undefined_syms_auto.{VERSION}.txt"]
    else:
        names = [f"symbols.{binary}.{VERSION}.txt", f"symbols.psyq.{binary}.{VERSION}.txt",
                 f"undefined_funcs_auto.{binary}.{VERSION}.txt", f"undefined_syms_auto.{binary}.{VERSION}.txt"]
    return [cfg / n for n in names if (cfg / n).exists()]


def read_symbols(path):
    """{name: (addr, is_func)} from a splat symbol file."""
    out = {}
    for line in path.read_text().splitlines():
        m = SYM_LINE.match(line)
        if m:
            out[m.group(1)] = (int(m.group(2), 16), "type:func" in m.group(3))
    return out


def functions():
    """{binary: {name: vram}} for every function, from the decomp's function index (names) plus addresses from
    the names themselves (func_XXXXXXXX) or the symbol files. The index's address column isn't used: it is off
    for overlays."""
    named = {}
    for b in ["main"] + [p.name[8:-7] for p in (DECOMP / "config").glob(f"symbols.ovl_*.{VERSION}.txt")]:
        for f in symbol_files(b):
            for n, (a, _) in read_symbols(f).items():
                named.setdefault(b, {}).setdefault(n, a)
    out = {}
    for line in (DECOMP / f"config/functions.{VERSION}.txt").read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        b, name = line.split()[:2]
        m = ADDR_NAME.match(name)
        addr = int(m.group(1), 16) if m else named.get(b, {}).get(name)
        if addr is None:
            addr = named.get("main", {}).get(name)  # PsyQ names live in main's files
        out.setdefault(b, {})[name] = addr
    return out


def data_symbols(bins):
    """{name: {addr, ...}} for every non-function symbol any binary knows: symbol files and splat's data labels.
    More than one address for a name is a conflict the caller reports."""
    out = {}

    def add(name, addr):
        out.setdefault(name, set()).add(addr)

    funcs = {n for fs in functions().values() for n in fs}
    for b in bins:
        for f in symbol_files(b):
            for n, (a, is_func) in read_symbols(f).items():
                if not is_func and n not in funcs:
                    add(n, a)
        asm = DECOMP / f"asm/{VERSION}/{'main' if b == 'main' else b}"
        for s in asm.rglob("*.s"):
            text = s.read_text(errors="replace")
            for m in re.finditer(r"^dlabel (\w+)\n\s*/\* [0-9A-F]+ ([0-9A-F]{8}) ", text, re.M):
                add(m.group(1), int(m.group(2), 16))
    return out


WORD_LINE = re.compile(r"/\* ([0-9A-F]+) ([0-9A-F]{8}) ([0-9A-F]{8}) \*/\s+\.word\s+(\S+)")


def data_words(binary):
    """(vram, value, operand) for every `.word` splat emitted in `binary`'s data/rodata: these are the only
    places a pointer can be (splat types everything else as .short/.byte/.asciz)."""
    asm = DECOMP / f"asm/{VERSION}/{binary}"
    seen = {}
    for s in sorted(asm.rglob("*.s")):
        for m in WORD_LINE.finditer(s.read_text(errors="replace")):
            vram = int(m.group(2), 16)
            raw = bytes.fromhex(m.group(3))
            seen[vram] = (vram, int.from_bytes(raw, "little"), m.group(4))
    return [seen[k] for k in sorted(seen)]
