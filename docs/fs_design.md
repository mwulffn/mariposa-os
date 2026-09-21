# Storage and Filesystems

> **Status.** Implemented: the kernel block layer (`blk.c`, `ide.c`), the
> block cache (`bcache.c`), the VFS (`vfs.c`), FAT16 read-only (`fs_fat16.c`)
> and **ext2 read-write** (`fs_ext2.c` over the shared `ext2.c`); the ROM
> boots from FAT16 or ext2. Pinned by `kblk.*`, `kvfs.*`, `kext2.*` and
> `disk.*`. Verified under FS-UAE end to end: booted from ext2, wrote files
> and a directory from the console, and the host's `e2fsck` passed the disk
> image afterwards. **Not yet:** a journal, triple indirection, symlinks
> followed, interrupt-driven IDE, PFS3.

## The plan

| Where | Filesystems |
|---|---|
| ROM | FAT16 read, ext2 read - enough to find and load the kernel, nothing more. ext2 is tried first: its magic number is a far better test than FAT's two bytes |
| Kernel | FAT16 read; ext2 read-write, the default system filesystem; others behind the VFS later (PFS3 is the one worth having) |

The ROM is ours, so nothing forces FAT16 to be special: the boot partition
may be either. ext2 is the default because it is simple, thoroughly
documented, and every modern host can build, check and mount it, which
matters most while there is no userspace to do any of that here. PFS3 is the
better filesystem for this machine - atomic commit, extents, Amiga metadata -
and is the intended second system filesystem once the VFS has been proven by
a first.

## Layers

```
console, loader, ...       vfs_open / vfs_read / vfs_readdir / ...
vfs.c                      volumes, paths, handles, the per-mount lock
fs_fat16.c  fs_ext2.c      filesystem drivers: struct fs_ops
bcache.c                   the block cache
blk.c                      block devices and partitions (RDB)
ide.c  ->  shared/ata.c    Gayle IDE, PIO
```

### Block devices

`struct blkdev` (shared with the ROM) gains `write` and a block count. The
kernel registers `ide0` and one device per RDB partition - `ide0p0`,
`ide0p1` - following `PART_NEXT` to the end of the list, which the ROM's
boot path never did. A partition is a window onto its parent: block 0 of
`ide0p1` is wherever the RDB says, and a read past its end fails rather than
reading the neighbour.

**IDE is polled PIO, and serialised by a mutex.** A transfer is a sequence
of register writes and a data loop; two tasks interleaving them would
corrupt both. This is the first user of a sleeping lock
(`docs/task_design.md`). Sleeping on the drive's interrupt instead of
polling its status is the obvious next step and needs Gayle's interrupt
registers; on a CF card, which answers at once, it buys little.

**Known issue: IDENTIFY DEVICE returns nothing under FS-UAE.** The drive
raises DRQ and then hands over 512 zero bytes - with either drive-select
value, from either data port address - while READ SECTORS through the same
loop works. So `ide0` has an unknown size there (`blocks` is 0, which means
"unknown", and the whole disk gets no bounds check); partitions take their
size from the RDB and are unaffected. The harness models IDENTIFY and
`kblk.disk_found_and_sized` passes against it, so this wants a real drive to
say which of the two is telling the truth.

### Solid state is the assumption

Every Amiga still running boots from CompactFlash or an SSD. There is no
seek. That changes what is worth doing: the unit of overhead is the ATA
*command*, not the distance between blocks, and reading from "disk" is only
about 2.5 times slower than copying from memory, because both are a 7MHz CPU
moving words. Two decisions follow, and they are the same two AmigaOS's FFS
made for other reasons - its `AddBuffers` cache held metadata, and
`MaxTransfer`/`Mask` governed data going straight to the caller.

**Metadata goes through the cache; bulk data goes round it.** Opening
`sys:docs/deep/big.dat` reads three directories, four inodes and the group
descriptors; reading it keeps returning to indirect blocks and writing it to
bitmaps. Each is a separate command for a few hundred bytes, repeated on
every operation - that is where a cache pays, and it needs tens of
kilobytes. Bulk data is the opposite: caching it on the way past costs a
copy and the bookkeeping, about as much as the transfer itself, to save 60%
of a re-read that may never come. So whole blocks of file data go straight
between the disk and the caller's memory - which, with no MMU, is just a
pointer: there is no kernel/user boundary to copy across. `struct blkdev` has
`bulk_read`/`bulk_write` for it. Partial blocks and small files still go
through the cache, since they need a buffer in any case; that covers the
commands, configuration and icons that are read again and again.

**Adjacent blocks move in one command.** `ext2_read`, `fs_ext2`'s write and
`fs_fat16`'s read each follow the block map while it stays contiguous and
issue one transfer for the run - up to 256 sectors, an ATA command's limit.
`mke2fs` lays files out in long runs, and this driver's allocator continues
from where the last block came from, so its files are contiguous too. A
512KB read went from 515 commands to 21.

Coherence is two rules. The cache is write-through, so the disk is never
behind it and a direct read needs nothing from it. A direct write drops any
cached copy of the blocks it covers. `kext2.w_direct_cached_agree` mixes
both paths over the same blocks; an odd buffer address, which word moves
cannot use, takes the cached road instead (`kext2.w_odd_buffer_address`).

### The block cache

Gayle's IDE is PIO with no DMA - every word of every sector goes through
the CPU - so a metadata block read twice is a command paid for twice.
Commodore could not spend RAM on this. This machine can: fast RAM spent to
save the slowest thing in it (`docs/driver_design.md`).

- 512-byte blocks, keyed by (device, block).
- **Sized from free memory, allocated at `bc_init`**: an eighth of what is
  free, fast RAM for preference, between 64 and 4096 blocks - about 115KB on
  a 1MB machine, about a megabyte on an 8MB one. It was a static 128KB array
  at first, which fixed the size whatever the machine had, hid the memory
  from every report, and grew the kernel image far enough to land on top of
  the test heaps. With no memory to be had the cache is off, and reads and
  writes go straight through.
- **`bc_read` coalesces.** A run of missing blocks is fetched with one ATA
  command, not one per block - the per-command overhead is what hurts.
- **Writes are write-through.** `bc_write` goes to the disk before it
  returns, and updates the cache. Delayed writes would be faster and make
  the order in which metadata reaches the disk - the thing crash safety
  rests on - the cache's decision instead of the filesystem's. That trade
  is not taken until a filesystem asks for it, with its eyes open.
- Least recently *used*, kept as a list so that the buffer to replace is
  always the tail: scanning a few thousand buffers on every miss would cost
  more than the disk read being avoided. Hit and miss counters, because a
  cache whose effect cannot be seen cannot be tuned.

Filesystem drivers never touch a block device; they get a `struct blkdev`
whose `read` and `write` go through the cache. That is how the shared
`fat16.c`, written for the ROM with no cache in mind, runs cached in the
kernel without a line changed.

### The VFS

**Names are Amiga-shaped: `volume:dir/file`.** A volume is a mounted
filesystem with a name - `boot:`, `sys:` - and there is no single root that
everything must hang from. A path with no volume is relative to the
caller's current volume, when there is such a thing.

**Lookup is case-insensitive and case-preserving**, as on AmigaOS. On a
case-sensitive filesystem (ext2) that means a directory can hold `Readme`
and `README` if a host tool put them there; lookup takes an exact match
first and otherwise the first match in directory order, and creating a name
that differs from an existing one only by case is refused.

**Handles, not pointers.** `vfs_open` returns a small integer; every call
validates it; a handle belongs to a task and is closed when the task exits
(`task_on_exit`). Same reasons as bitmaps: native libraries and Wasm host
calls will sit on top of this.

**One lock per mount.** Every operation on a volume holds that volume's
mutex. It is coarse, it is obviously correct, and the disk is the
bottleneck, not the lock.

A filesystem driver is a `struct fs_ops`: `mount`, `unmount`, `lookup`,
`read`, `readdir`, `stat`, and for a writable one `write`, `create`,
`remove`, `mkdir`, `truncate`, `sync`. A node is whatever the driver needs
it to be, opaque to the VFS.

## FAT16

Read-only, 8.3 names, subdirectories. Long file names are ignored: their
directory entries are skipped, and the 8.3 alias is what is seen. It exists
for the boot partition and for exchanging files with other machines, not as
somewhere to live.

## ext2

`src/shared/ext2.c` reads - superblock, group descriptors, inodes, the block
map through double indirection, directories - and is shared with the ROM,
so it allocates nothing and prints nothing. `src/kernel/fs_ext2.c` puts it
behind the VFS and adds writing.

**What is accepted:** revisions 0 and 1; 1K, 2K and 4K blocks; any inode
size; `filetype`, `sparse_super` and `large_file`. **Any other feature marked
incompatible makes the mount fail** - extents, a journal needing recovery,
64-bit block numbers. An ext4 volume mounted as ext2 would have its extent
trees read as block pointers. `htree` directories are compatible by design:
they are read as the plain lists they also are, and the index flag is
cleared on the first directory this driver adds an entry to, since it does
not maintain the index.

**Names:** ext2 is case-sensitive and the VFS is not, so a directory a host
wrote can hold `Readme` and `README`. Lookup takes an exact match first and
otherwise the first match in directory order; creating a name that differs
from an existing one only by case opens the existing one.

### Crash safety is in the order of writes

There is no journal. What stands in for one is the order things reach the
disk, and the block cache is write-through so that the order in the code is
the order on the platter:

| Operation | Order |
|---|---|
| growing a file | bitmap, then the data, then the inode that points at it |
| creating | bitmap, the inode, and only then the directory entry |
| removing | the directory entry first, then the inode, then the bitmaps |
| truncating | the inode, emptied, first; then its blocks freed from a copy |

Interrupt any of those anywhere and the worst on disk is a block or an inode
marked used that nothing refers to: a leak, which `e2fsck` reclaims. Never a
directory entry naming an inode that was not written, never a pointer to a
block somebody else may be given.

The superblock's free counts and the group descriptors change with every
allocation, so they are kept in memory and written at sync: closing a
written file, `vfs_sync`, unmount. The bitmaps are what actually prevent
double allocation, and they are always on disk before the inode that depends
on them. The superblock's state is cleared to "not clean" before the first
change and restored at sync; a volume found not clean is mounted with a
warning, not refused - the ordering above means it is leaky at worst.

### What it costs

Measured in cycles per kilobyte, which at 7.09MHz is a transfer rate. The
harness counts cycles; the console's `bench` command times a 1MB file against
the vertical blank on a running machine. Under FS-UAE's cycle-exact 68000
`bench` reports 673KB/s read and 345KB/s write, within 7% of what the
harness predicted - so the harness's numbers can be believed.

| 512KB in 32KB calls | cycles/KB | KB/s | ATA commands |
|---|---|---|---|
| read, as first written | 30,400 | 232 | 515 |
| read, now | 9,900 | 713 | 21 |
| write, as first written | 45,000 | 157 | 563 |
| write, now | 19,000 | 372 | 69 |

The PIO loop alone is about 6,500 cycles a KB, roughly 1MB/s: reads are
within 1.5x of what the hardware allows. The very first write path managed
50KB/s. Bounds are held by `kext2.read_throughput`, `w_throughput` and
`w_bulk_throughput`.

**How this compares with AmigaOS is not known.** Figures people report for
real machines - about 1.5MB/s on a 68000 A600 with a third-party driver,
2.2-2.4MB/s on accelerated A1200s - are raw device reads from SysInfo and
DriveSpeed, not file I/O through a filesystem, and none is a stock machine
measured the way `bench` measures. The 1.5MB/s is above this driver's PIO
ceiling, so that driver's inner loop is tighter than `ata_pio.s`. The
comparison that would settle it is `bench` against DiskSpeed on the same
real machine.

What got it here, in the order the profiler (`tools/profile.py`) pointed -
every one of which was somewhere other than where it was first looked for:

- The PIO loop and the cache's 512-byte copy in assembly (`ata_pio.s`,
  `blkcopy.s`). vbcc's loops cost about 48 cycles a word and 5,000 a block;
  these are about 13 and 1,500. Together they were 78% of a write, and the
  filesystem logic being tuned was 11%.
- Bulk data round the cache, and adjacent blocks in one command: above.
- Little-endian fields loaded whole and byte-swapped (`le.s`). Picking them
  apart a byte at a time in C cost about 250 cycles a field, a dozen fields
  per block allocated: 2.5M cycles of a 512KB write, more than the logic
  around them. FAT's fields sit at odd offsets and cannot use it.
- The allocator continues from where its last block came from instead of
  scanning each bitmap from bit 0 - 13,000 cycles a block once a group was
  mostly full - which is also what keeps files contiguous.
- Bitmaps and map blocks held in memory across a write call and flushed
  before the inode: the same order on disk, a fraction of the writes.
- Every `/` and `%` by a block size replaced by a shift or a mask.

### The price of the wrong byte order

ext2 is little-endian and the 68000 is big-endian, so every field this driver
reads or writes has to be turned round. **Amiga FFS never paid this, and
neither do PFS3 or SFS**: they were written for this CPU and store their
fields the way it reads them, where a field is one `move.l` of 12 to 16
cycles. Here it is a call to `le32_get` - push, `jsr`, load, two rotates and a
swap, `rts` - at around 130. On a bulk write the swap routines are 4% of all
cycles by the profiler, and about as much again goes on calling them: call it
8%, on the path where it matters least, since bulk data is never swapped -
only the structures around it. On metadata-heavy work the share is higher.

It was far worse before anyone looked: picking fields apart a byte at a time
in C cost about 250 cycles each and showed up as the largest single item in
a write. It could be made cheaper still by inlining the swap where vbcc
allows, and it cannot be made free. It is a standing cost of choosing a
filesystem for its tools instead of for its CPU, and a real point in PFS3's
favour when that comparison is made.

FAT is little-endian too, with the added insult that its boot sector has
fields at odd offsets, which the 68000 cannot load as words at all -
`fat16.c` still goes a byte at a time.

### Known slow: directories are linear

Creating a file in a directory of `n` entries scans all of them twice - once
to be sure the name is free, ignoring case, and once to find room - and each
step copies and compares a name. Creating 120 files in one directory costs
about 1.3 million cycles a file, a fifth of a second each on a 7MHz machine.
It is correct and it is what plain ext2 directories are; the fixes are a name
cache in front of lookup, comparing without copying, and eventually htree.
Not done.

### The judge is e2fsck

The write tests do not check their own work. After the guest has written,
the partition is dumped out of the machine (`h_disk_save`) and handed to the
real `e2fsck -fn`, which must find nothing at all, and to `debugfs`, which
reads the files back for comparison. The test image itself is made by the
real `mke2fs`. **e2fsprogs is therefore required** to run the tests; without
it `tests/mkdisk.py` stops with instructions. Breaking the driver five ways
- uncounted `i_blocks`, leaked indirect blocks, a stale group free count, a
missing parent link, a partial block written blind - was caught five times,
four of them by `e2fsck` and not by anything written here.

### Not yet

- A journal (ext3's is an addition, not a reformat).
- Triple indirection: files stop at about 64MB with 1K blocks, 4GB with 4K.
  Refused cleanly, not written wrongly.
- Symlinks are listed and cannot be opened.
- Amiga protection bits and file comments, which would go in extended
  attributes.
- Timestamps: there is no clock, so new files get the volume's last write
  time.
