#!/usr/bin/env python3
"""
mkdisk.py - build the disk images the headless disk tests run against.

Produces a raw IDE image with an Amiga Rigid Disk Block, one partition, and a
FAT16 filesystem containing SYSTEM.BIN - the exact shape src/rom/partition.s
and src/rom/filesystem.s expect to find. No mtools, no emulator, no boot.hdf.

Also emits disk_layout.h so the tests assert against the values that were
actually written rather than a second, drifting copy of them.

Two details are deliberate:
  - SYSTEM.BIN's cluster chain is scattered, not contiguous, so a broken
    fat16_get_next_cluster cannot pass by accident.
  - its contents are position dependent, so clusters loaded out of order or
    skipped are detectable byte for byte.

Usage: mkdisk.py <output-dir>
"""

import os
import struct
import sys

SECTOR = 512

# Geometry. Chosen so LowCyl * Heads stays under 65536: partition.s computes
# the start LBA with mulu.w, which truncates the intermediate to 16 bits.
HEADS = 4
SECTORS = 32
LOWCYL = 2
HIGHCYL = 65
TOTAL_CYL = 66

PART_START = LOWCYL * HEADS * SECTORS  # 256
PART_SIZE = (HIGHCYL - LOWCYL + 1) * HEADS * SECTORS  # 8192
TOTAL_SECTORS = TOTAL_CYL * HEADS * SECTORS  # 8448

# FAT16 parameters. Sized to land above the 4085-cluster floor so this is a
# genuine FAT16 volume and not a FAT12 one wearing its name.
BYTES_PER_SEC = 512
SEC_PER_CLUS = 1
RSVD = 1
NUM_FATS = 2
ROOT_ENT = 512
FAT_SIZE = 32

ROOT_DIR_START = RSVD + NUM_FATS * FAT_SIZE  # 65
ROOT_DIR_SECS = ROOT_ENT * 32 // BYTES_PER_SEC  # 32
DATA_START = ROOT_DIR_START + ROOT_DIR_SECS  # 97
CLUSTER_COUNT = PART_SIZE - DATA_START  # 8095

SYSBIN_SIZE = 5000
SYSBIN_CHAIN = [2, 9, 3, 15, 4, 20, 5, 30, 6, 40]

# README.TXT sits far enough up that its FAT entry lands in a different
# FAT sector from SYSTEM.BIN's, which is what exercises the single-sector
# FAT cache in fat16_get_next_cluster.
README_CLUSTER = 4000

# A LowCyl large enough that LowCyl * Heads overflows 16 bits.
BIGCYL_LOWCYL = 20000
BIGCYL_HIGHCYL = 20001


def sysbin_byte(i):
    return (i * 31 + 7) & 0xFF


def sysbin_data():
    return bytes(sysbin_byte(i) for i in range(SYSBIN_SIZE))


# --------------------------------------------------------------------- RDB


def amiga_checksum(block, summed_longs):
    """Amiga block checksum: the summed longs add up to zero."""
    total = 0
    for i in range(summed_longs):
        if i == 2:  # skip the checksum field itself
            continue
        total += struct.unpack_from(">I", block, i * 4)[0]
    return (-total) & 0xFFFFFFFF


def rdb_block(lowcyl_unused=None):
    b = bytearray(SECTOR)
    struct.pack_into(">4sII", b, 0, b"RDSK", 64, 0)  # id, summedlongs, chksum
    struct.pack_into(">I", b, 12, 7)  # hostid
    struct.pack_into(">I", b, 16, SECTOR)  # blockbytes
    struct.pack_into(">I", b, 20, 0)  # flags
    struct.pack_into(">I", b, 24, 0xFFFFFFFF)  # badblocklist
    struct.pack_into(">I", b, 28, 1)  # partitionlist -> LBA 1
    struct.pack_into(">I", b, 32, 0xFFFFFFFF)  # filesysheaderlist
    struct.pack_into(">I", b, 36, 0xFFFFFFFF)  # driveinit
    struct.pack_into(">I", b, 64, TOTAL_CYL)
    struct.pack_into(">I", b, 68, SECTORS)
    struct.pack_into(">I", b, 72, HEADS)
    struct.pack_into(">I", b, 8, amiga_checksum(b, 64))
    return bytes(b)


def part_block(lowcyl, highcyl, name=b"DH0"):
    b = bytearray(SECTOR)
    struct.pack_into(">4sII", b, 0, b"PART", 64, 0)
    struct.pack_into(">I", b, 12, 7)  # hostid
    struct.pack_into(">I", b, 16, 0xFFFFFFFF)  # next: end of list
    struct.pack_into(">I", b, 20, 1)  # flags: bootable
    struct.pack_into(">I", b, 32, 0)  # devflags
    b[36] = len(name)  # BCPL string
    b[37 : 37 + len(name)] = name

    env = 128
    struct.pack_into(">I", b, env + 0, 16)  # tablesize
    struct.pack_into(">I", b, env + 4, SECTOR // 4)  # sizeblock, in longs
    struct.pack_into(">I", b, env + 12, HEADS)  # surfaces
    struct.pack_into(">I", b, env + 16, 1)  # sectorsperblock
    struct.pack_into(">I", b, env + 20, SECTORS)  # blockspertrack
    struct.pack_into(">I", b, env + 24, 2)  # reserved
    struct.pack_into(">I", b, env + 36, lowcyl)
    struct.pack_into(">I", b, env + 40, highcyl)
    struct.pack_into(">I", b, env + 44, 30)  # numbuffers
    struct.pack_into(">I", b, env + 64, 0x46415431)  # dostype 'FAT1'
    struct.pack_into(">I", b, 8, amiga_checksum(b, 64))
    return bytes(b)


# ------------------------------------------------------------------- FAT16


def boot_sector():
    b = bytearray(SECTOR)
    b[0:3] = b"\xeb\x3c\x90"
    b[3:11] = b"MARIPOSA"
    struct.pack_into("<H", b, 11, BYTES_PER_SEC)
    b[13] = SEC_PER_CLUS
    struct.pack_into("<H", b, 14, RSVD)
    b[16] = NUM_FATS
    struct.pack_into("<H", b, 17, ROOT_ENT)
    struct.pack_into("<H", b, 19, PART_SIZE)  # totsec16
    b[21] = 0xF8  # media: fixed disk
    struct.pack_into("<H", b, 22, FAT_SIZE)
    struct.pack_into("<H", b, 24, SECTORS)
    struct.pack_into("<H", b, 26, HEADS)
    struct.pack_into("<I", b, 28, PART_START)  # hidden sectors
    struct.pack_into("<I", b, 32, 0)  # totsec32
    b[36] = 0x80  # drive number
    b[38] = 0x29  # extended boot sig
    struct.pack_into("<I", b, 39, 0x4D415249)
    b[43:54] = b"MARIPOSA   "
    b[54:62] = b"FAT16   "
    b[510] = 0x55
    b[511] = 0xAA
    return bytes(b)


def fat_table():
    entries = [0xFFF8, 0xFFFF] + [0x0000] * (CLUSTER_COUNT)
    for i, cluster in enumerate(SYSBIN_CHAIN):
        last = i == len(SYSBIN_CHAIN) - 1
        entries[cluster] = 0xFFFF if last else SYSBIN_CHAIN[i + 1]
    entries[README_CLUSTER] = 0xFFFF

    raw = bytearray()
    for e in entries:
        raw += struct.pack("<H", e)
    raw += b"\x00" * (FAT_SIZE * SECTOR - len(raw))
    return bytes(raw[: FAT_SIZE * SECTOR])


def dir_entry(name, attr, cluster, size):
    e = bytearray(32)
    e[0:11] = name
    e[11] = attr
    struct.pack_into("<H", e, 22, 0x6000)  # time
    struct.pack_into("<H", e, 24, 0x5A21)  # date
    struct.pack_into("<H", e, 26, cluster)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def root_dir():
    """Decoys first, so the scan logic is actually exercised."""
    d = bytearray()
    d += dir_entry(b"MARIPOSA   ", 0x08, 0, 0)  # volume label
    deleted = bytearray(dir_entry(b"OLDFILE BIN", 0x20, 99, 123))
    deleted[0] = 0xE5  # deleted entry
    d += bytes(deleted)
    d += dir_entry(b"README  TXT", 0x20, README_CLUSTER, 64)
    d += dir_entry(b"SYSTEM  BIN", 0x20, SYSBIN_CHAIN[0], SYSBIN_SIZE)
    d += b"\x00" * (ROOT_DIR_SECS * SECTOR - len(d))
    return bytes(d[: ROOT_DIR_SECS * SECTOR])


def build_partition():
    part = bytearray(PART_SIZE * SECTOR)

    def put(sector, data):
        part[sector * SECTOR : sector * SECTOR + len(data)] = data

    put(0, boot_sector())
    fat = fat_table()
    for n in range(NUM_FATS):
        put(RSVD + n * FAT_SIZE, fat)
    put(ROOT_DIR_START, root_dir())

    data = sysbin_data()
    for i, cluster in enumerate(SYSBIN_CHAIN):
        chunk = data[i * SECTOR * SEC_PER_CLUS : (i + 1) * SECTOR * SEC_PER_CLUS]
        put(DATA_START + (cluster - 2) * SEC_PER_CLUS, chunk)

    return bytes(part)


# -------------------------------------------------------------------- images


def build_main_image():
    img = bytearray(TOTAL_SECTORS * SECTOR)
    img[0:SECTOR] = rdb_block()
    img[SECTOR : 2 * SECTOR] = part_block(LOWCYL, HIGHCYL)
    part = build_partition()
    img[PART_START * SECTOR : PART_START * SECTOR + len(part)] = part
    return bytes(img)


def build_blank_image():
    """No RDB anywhere in blocks 0-15, so find_rdb must report not found."""
    return b"\x00" * (64 * SECTOR)


def build_bigcyl_image():
    """RDB and PART only. LowCyl is large enough to overflow partition.s's
    16x16 multiply, which is the point of the image."""
    img = bytearray(64 * SECTOR)
    img[0:SECTOR] = rdb_block()
    img[SECTOR : 2 * SECTOR] = part_block(BIGCYL_LOWCYL, BIGCYL_HIGHCYL)
    return bytes(img)


# -------------------------------------------------------------------- verify


def verify(img):
    """Re-read the image through its own BPB, not through the constants above.
    Catches layout mistakes that a writer-only check would sail past."""
    base = PART_START * SECTOR
    bs = img[base : base + SECTOR]

    if bs[510] != 0x55 or bs[511] != 0xAA:
        raise AssertionError("boot signature missing")

    bps = struct.unpack_from("<H", bs, 11)[0]
    spc = bs[13]
    rsvd = struct.unpack_from("<H", bs, 14)[0]
    nfats = bs[16]
    rootent = struct.unpack_from("<H", bs, 17)[0]
    fatsz = struct.unpack_from("<H", bs, 22)[0]

    root_start = rsvd + nfats * fatsz
    root_secs = (rootent * 32 + bps - 1) // bps
    data_start = root_start + root_secs

    if (root_start, root_secs, data_start) != (
        ROOT_DIR_START,
        ROOT_DIR_SECS,
        DATA_START,
    ):
        raise AssertionError("derived geometry disagrees with the constants")

    # Find SYSTEM.BIN by scanning, the way filesystem.s does.
    found = None
    for s in range(root_secs):
        sec = img[base + (root_start + s) * bps : base + (root_start + s + 1) * bps]
        for o in range(0, bps, 32):
            e = sec[o : o + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5 or (e[11] & 0x08):
                continue
            if e[0:11] == b"SYSTEM  BIN":
                found = (
                    struct.unpack_from("<H", e, 26)[0],
                    struct.unpack_from("<I", e, 28)[0],
                )
        if found:
            break
    if not found:
        raise AssertionError("SYSTEM.BIN not found by an independent scan")

    cluster, size = found
    if size != SYSBIN_SIZE:
        raise AssertionError("size %d != %d" % (size, SYSBIN_SIZE))

    # Walk the FAT chain and reassemble the file.
    out = bytearray()
    seen = set()
    while cluster < 0xFFF8:
        if cluster in seen:
            raise AssertionError("cluster chain loops at %d" % cluster)
        seen.add(cluster)
        lba = data_start + (cluster - 2) * spc
        out += img[base + lba * bps : base + (lba + spc) * bps]
        off = cluster * 2
        fat_sec = rsvd + off // bps
        entry = img[base + fat_sec * bps + (off % bps) :][:2]
        cluster = struct.unpack("<H", entry)[0]

    if bytes(out[:size]) != sysbin_data():
        raise AssertionError("reassembled SYSTEM.BIN does not match")
    if len(seen) != len(SYSBIN_CHAIN):
        raise AssertionError("chain length %d != %d" % (len(seen), len(SYSBIN_CHAIN)))


# -------------------------------------------------------------------- header

HEADER = """\
/* Generated by tests/mkdisk.py - do not edit.
 *
 * The values the disk images were actually built with. Tests assert against
 * these so there is one source of truth for the layout.
 */
#ifndef DISK_LAYOUT_H
#define DISK_LAYOUT_H

#define DISK_IMAGE              "test-disk.img"
#define DISK_BLANK_IMAGE        "blank.img"
#define DISK_BIGCYL_IMAGE       "bigcyl.img"

#define DISK_TOTAL_SECTORS      %(total_sectors)u
#define DISK_HEADS              %(heads)u
#define DISK_SECTORS            %(sectors)u
#define DISK_LOWCYL             %(lowcyl)u
#define DISK_HIGHCYL            %(highcyl)u
#define DISK_PART_START_LBA     %(part_start)u
#define DISK_PART_SECTORS       %(part_size)u

#define DISK_FAT_BYTES_PER_SEC  %(bps)u
#define DISK_FAT_SEC_PER_CLUS   %(spc)u
#define DISK_FAT_RSVD           %(rsvd)u
#define DISK_FAT_NUM_FATS       %(nfats)u
#define DISK_FAT_ROOT_ENT       %(rootent)u
#define DISK_FAT_SIZE           %(fatsz)u
#define DISK_FAT_ROOT_START     %(root_start)u
#define DISK_FAT_ROOT_SECS      %(root_secs)u
#define DISK_FAT_DATA_START     %(data_start)u

#define DISK_SYSBIN_SIZE        %(sysbin_size)u
#define DISK_SYSBIN_CLUSTERS    %(nclusters)u
#define DISK_SYSBIN_CHAIN       { %(chain)s }
/* In a different FAT sector from the chain above, on purpose. */
#define DISK_README_CLUSTER     %(readme)u
#define DISK_README_FAT_SECTOR  %(readme_sec)u
#define DISK_SYSBIN_BYTE(i)     ((unsigned char)(((i) * 31u + 7u) & 0xFFu))

/* LowCyl * Heads here exceeds 65535, which partition.s's mulu.w cannot hold. */
#define DISK_BIGCYL_LOWCYL      %(bigcyl)u
#define DISK_BIGCYL_START_LBA   %(bigcyl_start)uu

#endif /* DISK_LAYOUT_H */
"""


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(__doc__.strip() + "\n")
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)

    img = build_main_image()
    try:
        verify(img)
    except AssertionError as e:
        sys.stderr.write("mkdisk: self-check failed: %s\n" % e)
        return 2

    with open(os.path.join(out, "test-disk.img"), "wb") as f:
        f.write(img)
    with open(os.path.join(out, "blank.img"), "wb") as f:
        f.write(build_blank_image())
    with open(os.path.join(out, "bigcyl.img"), "wb") as f:
        f.write(build_bigcyl_image())

    with open(os.path.join(out, "disk_layout.h"), "w") as f:
        f.write(
            HEADER
            % {
                "total_sectors": TOTAL_SECTORS,
                "heads": HEADS,
                "sectors": SECTORS,
                "lowcyl": LOWCYL,
                "highcyl": HIGHCYL,
                "part_start": PART_START,
                "part_size": PART_SIZE,
                "bps": BYTES_PER_SEC,
                "spc": SEC_PER_CLUS,
                "rsvd": RSVD,
                "nfats": NUM_FATS,
                "rootent": ROOT_ENT,
                "fatsz": FAT_SIZE,
                "root_start": ROOT_DIR_START,
                "root_secs": ROOT_DIR_SECS,
                "data_start": DATA_START,
                "sysbin_size": SYSBIN_SIZE,
                "nclusters": len(SYSBIN_CHAIN),
                "chain": ", ".join(str(c) for c in SYSBIN_CHAIN),
                "readme": README_CLUSTER,
                "readme_sec": README_CLUSTER * 2 // BYTES_PER_SEC,
                "bigcyl": BIGCYL_LOWCYL,
                "bigcyl_start": BIGCYL_LOWCYL * HEADS * SECTORS,
            }
        )

    sys.stderr.write(
        "mkdisk: %d sectors, partition at LBA %d, %d clusters, SYSTEM.BIN %d bytes\n"
        % (TOTAL_SECTORS, PART_START, CLUSTER_COUNT, SYSBIN_SIZE)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
