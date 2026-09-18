#!/usr/bin/env python3
"""
Verify memory detection against the ROM's memory map table.

Drives the ROM debugger over serial and checks MEMMAP_TABLE ($3250) entry by
entry against what the machine is configured to have. Expectations are derived
from the FS-UAE config that `make run` will actually launch - read out of the
Makefile so the two cannot diverge - rather than hardcoded. Hardcoding is what
made the previous version of this script rot: it read four variables at $460
that the ROM has not used for a long time.

This tier needs FS-UAE and a display. Anything that is pure logic belongs in
the headless suite instead:  make test

Usage: ./test_memory_config.py [config.fs-uae]
"""

import os
import re
import signal
import socket
import subprocess
import sys
import time

# From src/rom/hardware.i
MEMMAP_TABLE = 0x3250
KERNEL_CHIP = 0x4000            # start of kernel-managed chip RAM
FAST_BASE = 0x200000            # where Zorro II RAM is relocated to
KERNEL_STACK_SIZE = 0x2000      # reserved at the top of fast RAM
ROM_BASE = 0xFC0000
ROM_SIZE = 0x40000

MEM_TYPE_CHIP = 1
MEM_TYPE_FAST = 2
MEM_TYPE_ROM = 5
MEM_TYPE_RESERVED = 6

TYPE_NAME = {0: "End", 1: "Chip", 2: "Fast", 5: "ROM", 6: "Reserved"}

ENTRY_LONGS = 3                 # base, size, (type << 16) | flags


def config_from_makefile():
    """Whichever config `make run` will launch."""
    try:
        m = re.search(r'^CONFIG\s*=\s*(\S+)', open('Makefile').read(), re.M)
        if m:
            return m.group(1)
    except OSError:
        pass
    return 'configs/a600.fs-uae'


def parse_config(path):
    """Chip and fast RAM sizes in bytes, from an FS-UAE config."""
    chip_kb = fast_kb = 0
    for line in open(path):
        m = re.match(r'\s*(chip_memory|fast_memory)\s*=\s*(\d+)', line)
        if m:
            if m.group(1) == 'chip_memory':
                chip_kb = int(m.group(2))
            else:
                fast_kb = int(m.group(2))
    return chip_kb * 1024, fast_kb * 1024


def expected_entries(chip_bytes, fast_bytes):
    """What build_memory_table in src/rom/memory.s should have produced."""
    entries = [
        (0x000000, KERNEL_CHIP, MEM_TYPE_RESERVED, 1, "reserved low chip"),
        (KERNEL_CHIP, chip_bytes - KERNEL_CHIP, MEM_TYPE_CHIP, 1, "chip RAM"),
    ]
    if fast_bytes:
        usable = fast_bytes - KERNEL_STACK_SIZE
        entries.append((FAST_BASE, usable, MEM_TYPE_FAST, 1, "fast RAM"))
        entries.append((FAST_BASE + usable, KERNEL_STACK_SIZE,
                        MEM_TYPE_RESERVED, 1, "kernel stack"))
    entries.append((ROM_BASE, ROM_SIZE, MEM_TYPE_ROM, 0, "ROM"))
    entries.append((0, 0, 0, 0, "terminator"))
    return entries


class MemoryTest:
    def __init__(self, config):
        self.config = config
        self.emulator = None
        self.sock = None
        self.boot_log = ""
        self.passed = 0
        self.failed = 0

    # --- reporting -------------------------------------------------------

    def ok(self, what):
        print("  PASS  %s" % what)
        self.passed += 1

    def bad(self, what, detail=""):
        print("  FAIL  %s" % what)
        if detail:
            for line in detail.rstrip().split('\n'):
                print("          %s" % line)
        self.failed += 1

    # --- emulator --------------------------------------------------------

    def start(self):
        print("Starting FS-UAE with %s ..." % self.config)
        self.emulator = subprocess.Popen(
            ['make', 'run'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid)
        time.sleep(4)

    def connect(self):
        print("Connecting to the debugger on localhost:5555 ...", end='', flush=True)
        for attempt in range(10):
            try:
                if self.sock:
                    self.sock.close()
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(('localhost', 5555))
                print(" connected")
                time.sleep(1)
                # Keep the boot output rather than discarding it - the printed
                # memory map is worth checking too.
                self.sock.settimeout(0.5)
                try:
                    while True:
                        chunk = self.sock.recv(4096)
                        if not chunk:
                            break
                        self.boot_log += chunk.decode('ascii', errors='replace')
                except socket.timeout:
                    pass
                self.sock.settimeout(5.0)
                return True
            except (ConnectionRefusedError, OSError) as e:
                if attempt < 9:
                    print('.', end='', flush=True)
                    time.sleep(1)
                else:
                    print(" failed: %s" % e)
                    return False
        return False

    def command(self, cmd):
        self.sock.sendall((cmd + '\n').encode())
        time.sleep(0.3)
        out = b''
        self.sock.settimeout(1.0)
        try:
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        self.sock.settimeout(5.0)
        return out.decode('ascii', errors='replace')

    def stop(self):
        print("\nCleaning up ...")
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        if self.emulator:
            try:
                os.killpg(os.getpgid(self.emulator.pid), signal.SIGTERM)
                self.emulator.wait(timeout=5)
            except (subprocess.TimeoutExpired, ProcessLookupError, OSError):
                try:
                    os.killpg(os.getpgid(self.emulator.pid), signal.SIGKILL)
                except OSError:
                    pass

    # --- reading memory --------------------------------------------------

    def read_longs(self, addr, count):
        """Read `count` longwords using the debugger's 'm.l' (4 per dump)."""
        longs = []
        raw = []
        while len(longs) < count:
            reply = self.command("m.l %x" % (addr + len(longs) * 4))
            raw.append(reply)
            m = re.search(r'\$[0-9A-Fa-f]{8}:((?:\s+[0-9A-Fa-f]{8}){4})', reply)
            if not m:
                return None, ''.join(raw)
            longs += [int(v, 16) for v in m.group(1).split()]
        return longs[:count], ''.join(raw)

    # --- the checks ------------------------------------------------------

    def check_boot_log(self):
        print("\nBoot output")
        if "Memory Map:" in self.boot_log:
            self.ok("ROM printed its memory map")
        else:
            self.bad("ROM printed its memory map",
                     "captured %d bytes, no 'Memory Map:' header:\n%s"
                     % (len(self.boot_log), self.boot_log[-400:]))
        for name in ("Reserved", "Chip", "ROM"):
            if name in self.boot_log:
                self.ok("memory map mentions %s" % name)
            else:
                self.bad("memory map mentions %s" % name)

    def check_table(self, expected):
        print("\nMemory map table at $%06X" % MEMMAP_TABLE)
        longs, raw = self.read_longs(MEMMAP_TABLE, len(expected) * ENTRY_LONGS)
        if longs is None:
            self.bad("read the table",
                     "could not parse a dump out of:\n%s" % raw[-400:])
            return None

        for i, (base, size, mtype, flags, name) in enumerate(expected):
            got_base = longs[i * 3]
            got_size = longs[i * 3 + 1]
            packed = longs[i * 3 + 2]
            got_type, got_flags = packed >> 16, packed & 0xFFFF

            if (got_base, got_size, got_type, got_flags) == (base, size, mtype, flags):
                self.ok("entry %d %-18s $%08X +$%08X %s"
                        % (i, name, base, size, TYPE_NAME.get(mtype, "?")))
            else:
                self.bad("entry %d %s" % (i, name),
                         "expected base $%08X size $%08X type %d flags $%04X\n"
                         "got      base $%08X size $%08X type %d flags $%04X"
                         % (base, size, mtype, flags,
                            got_base, got_size, got_type, got_flags))
        return longs

    def check_fast_ram_access(self, expected):
        fast = [e for e in expected if e[2] == MEM_TYPE_FAST]
        if not fast:
            print("\nFast RAM access: no fast RAM configured, skipping")
            return
        base, size = fast[0][0], fast[0][1]
        print("\nFast RAM read/write")
        # Somewhere near the bottom and somewhere near the top of what the
        # ROM says is usable - derived, so this cannot outlive the config.
        for addr, value in ((base + 0x1000, 0xDEADBEEF),
                            (base + size - 0x1000, 0xCAFEBABE)):
            self.command("m %x %08X" % (addr, value))
            reply = self.command("m.l %x" % addr)
            m = re.search(r'\$[0-9A-Fa-f]{8}:\s+([0-9A-Fa-f]{8})', reply)
            if m and int(m.group(1), 16) == value:
                self.ok("wrote and read back $%08X at $%06X" % (value, addr))
            else:
                self.bad("wrote and read back $%08X at $%06X" % (value, addr),
                         "reply was: %s" % reply.strip())

    # --- driver ----------------------------------------------------------

    def run(self):
        chip, fast = parse_config(self.config)
        if not chip:
            print("Could not read chip_memory from %s" % self.config)
            return 2

        print("=" * 68)
        print("MEMORY CONFIGURATION TEST")
        print("=" * 68)
        print("config     %s" % self.config)
        print("chip RAM   %d KB" % (chip // 1024))
        print("fast RAM   %d KB" % (fast // 1024))

        expected = expected_entries(chip, fast)

        try:
            self.start()
            if not self.connect():
                print("\nCould not reach the debugger. The ROM only drops into it "
                      "when\nboot fails - if a bootable SYSTEM.BIN is present it "
                      "runs the kernel\ninstead and never reaches the prompt.")
                return 2

            self.check_boot_log()
            self.check_table(expected)
            self.check_fast_ram_access(expected)

            print("\n" + "=" * 68)
            print("%d passed, %d failed" % (self.passed, self.failed))
            return 0 if self.failed == 0 else 1
        finally:
            self.stop()


def main(argv):
    config = argv[1] if len(argv) > 1 else config_from_makefile()
    if not os.path.exists(config):
        print("No such config: %s" % config)
        return 2
    return MemoryTest(config).run()


if __name__ == '__main__':
    sys.exit(main(sys.argv))
