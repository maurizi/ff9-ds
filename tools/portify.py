#!/usr/bin/env python3
"""Copy a decomp C file into build/ with the PS1-only bits adapted. The decomp checkout is never modified.

Usage: portify.py SRC.c OUT.c [--report FILE]

- Drops `__asm__("... .rodata ...")` / `.rdata` padding statements: they only align PS1 rodata for matching.
  Any other `__asm__(` left at statement level is an error: the port has no MIPS.
- Hardware addresses `0x1F80xxxx` become `PSX_HW(0x1F80xxxx)` (scratchpad, SPU registers; port/include/psx_mem.h).
- RAM addresses `0x80xxxxxx` cast to a pointer, `(T *)0x80xxxxxx` or `(T *)(0x80xxxxxx ...`, become
  `PSX_ADDR(0x80xxxxxx)` (the psx_ram image).
- Every other `0x80xxxxxx` literal in [0x80010000, 0x80200000) is an address held in an integer (allocator tops,
  buffer bases): it is wrapped too, and listed in the report for review against decomp-requests.md R4. Literals
  that are really plain numbers go in config/addr_plain.txt (`path:line literal`) and are left alone.
"""
import argparse
import re
import sys
from pathlib import Path

PLAIN_FILE = Path(__file__).resolve().parent.parent / "config" / "addr_plain.txt"

PAD = re.compile(r'__asm__\("(?:\.section \.rodata|\.rdata)[^"]*"\);')
HW = re.compile(r'(?<![\w(])0x1F80[0-9A-Fa-f]{4}\b')
CAST_RAM = re.compile(r'(\(\s*[A-Za-z_][\w\s]*\*+\s*\)\s*\(?\s*)(0x80[0-9A-Fa-f]{6})\b')
RAM = re.compile(r'\b0x80[0-9A-Fa-f]{6}\b')


def strip_comments_keep_lines(text):
    """Blank out comments and string literals (keeping newlines) so the regexes only see code."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + " " * (j - i - 1) + (c if j < n else ""))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def substitute(text, code, pattern, repl):
    """Apply pattern to `code` (the comment-free view) and splice replacements into `text` at the same spans."""
    pieces, last = [], 0
    for m in pattern.finditer(code):
        pieces.append(text[last : m.start()])
        pieces.append(repl(m))
        last = m.end()
    pieces.append(text[last:])
    return "".join(pieces)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--report")
    args = ap.parse_args()

    text = open(args.src, encoding="utf-8").read()
    text = PAD.sub("/* port: rodata padding removed */", text)

    code = strip_comments_keep_lines(text)
    bad = [code.count("\n", 0, m.start()) + 1 for m in re.finditer(r"(?<![A-Za-z_])__asm__\s*\(", code)]
    if bad:
        sys.exit(f"{args.src}: inline asm the port can't handle on lines {bad}")

    text = substitute(text, code, HW, lambda m: f"PSX_HW({m.group(0)})")
    code = strip_comments_keep_lines(text)
    text = substitute(text, code, CAST_RAM, lambda m: f"{m.group(1)}PSX_ADDR({m.group(2)})")
    code = strip_comments_keep_lines(text)

    plain = set()
    for entry in open(PLAIN_FILE, encoding="utf-8"):
        entry = entry.split("#")[0].split()
        if entry:
            plain.add((entry[0], entry[1].upper()))
    report = []

    def wrap(m):
        v = int(m.group(0), 16)
        line = code.count("\n", 0, m.start()) + 1
        where = f"{args.src.removeprefix('decomp/')}:{line}"
        if not 0x80010000 <= v < 0x80200000 or code[max(0, m.start() - 9) : m.start()].endswith("PSX_ADDR("):
            return m.group(0)
        if (where, m.group(0).upper()) in plain:
            return m.group(0)
        report.append(f"{where}: {m.group(0)}: {text.splitlines()[line - 1].strip()}")
        return f"PSX_ADDR({m.group(0)})"

    text = substitute(text, code, RAM, wrap)

    with open(args.out, "w", encoding="utf-8") as f:
        f.write(f'#line 1 "{args.src}"\n' + text)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            f.write("".join(r + "\n" for r in report))


if __name__ == "__main__":
    main()
