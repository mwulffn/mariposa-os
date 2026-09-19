#!/usr/bin/env python3
"""
mksym.py - turn a vasm listing into a flat symbol file for the test harness.

The ROM is a raw binary with no symbol table, so tests would otherwise have
to hardcode addresses and would silently rot on every edit. vasm's listing
(-L) carries the symbol table at the end, in one of two formats depending on
the vasm version.

Up to vasm 1.9:

    panic_with_msg LAB (0xfc045e) sec=seg00fc0000
    SPRINTF_BUFFER EXPR(13312)

vasm 2.0 and later, under "Symbols by name:", with the value in hex behind a
tag: A for an absolute label, E for an expression/equate, or a two-digit
section number for a section-relative label (optionally marked EXP):

    panic_with_msg                   A:00FC0506
    SPRINTF_BUFFER                   E:00003400
    __divu                           00:00000000 EXP

Both are accepted. Output is one "name hexvalue" per line. Only global labels
and equates are present - vasm does not export local labels (.foo), which is
why tests can only target the routine entry points.

Usage: mksym.py <listing> <output.sym>
"""

import re
import sys

LAB = re.compile(r"^(\S+)\s+LAB\s*\(0x([0-9a-fA-F]+)\)")
EXPR = re.compile(r"^(\S+)\s+EXPR\s*\(\s*(-?\d+)\s*\)")

# vasm 2.0+: "NAME  A:00FC0506" (absolute), "NAME  E:00000400" (equate), or
# "NAME  00:0000005A EXP" (relative to section 00, as libsup.s assembles).
# A section-relative value is the offset from the section base, which is what
# h_load_module()/h_add_symbols() want: they supply the load bias themselves.
# Anchored at end of line so nothing in the disassembly body can match.
TAGGED = re.compile(r"^(\S+)\s+(?:[AE]|\d+):([0-9a-fA-F]+)(?:\s+EXP)?\s*$")


def parse(path):
    syms = {}
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = LAB.match(line)
            if m:
                syms[m.group(1)] = int(m.group(2), 16)
                continue
            m = TAGGED.match(line)
            if m:
                syms[m.group(1)] = int(m.group(2), 16)
                continue
            m = EXPR.match(line)
            if m:
                value = int(m.group(2))
                if value >= 0:
                    syms[m.group(1)] = value
    return syms


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__.strip() + "\n")
        return 2

    listing, out = argv[1], argv[2]
    try:
        syms = parse(listing)
    except OSError as e:
        sys.stderr.write("mksym: %s\n" % e)
        return 2

    if not syms:
        sys.stderr.write(
            "mksym: no symbols found in %s\n"
            "       this vasm version may format its listing differently;\n"
            '       the harness needs "NAME LAB (0xADDR)" / "NAME EXPR(n)"\n'
            '       or "NAME A:ADDR" / "NAME E:VALUE" lines\n' % listing
        )
        return 2

    with open(out, "w") as f:
        for name in sorted(syms):
            f.write("%s %x\n" % (name, syms[name]))

    sys.stderr.write("mksym: %d symbols -> %s\n" % (len(syms), out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
